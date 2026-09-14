#!/usr/bin/env python3
"""
Goldeneye 007 ROM Driver
======================

File extraction and table parsing for Goldeneye 007 N64 ROMs.
Supports vanilla and patched ROMs through table validation and scanning.

Based on research from goldeneye_docs:
- rom-address-table.txt: File table locations and formats
- note on compression.txt: 0x1172 compression header
- TLB index NGEE.txt: RAM to ROM address conversion

Table Locations (NTSC-Final vanilla):
- Backgrounds: 0x438660 (37 entries)
- Characters: 0x6ecb90 (64 entries)
- Music: 0x419794 (63 entries)
"""

import struct
import zlib
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import json
import sys

# Import scanner if available
try:
    from rom_scanner import ROMScanner
except ImportError:
    # Try relative import
    try:
        from .rom_scanner import ROMScanner
    except ImportError:
        ROMScanner = None


class GEROMDriver:
    """Goldeneye 007 ROM file extraction driver"""
    
    # Vanilla table offsets for NTSC-Final (U)
    VANILLA_TABLES_NTSC = {
        'backgrounds': 0x438660,
        'characters': 0x6ecb90,
        'music': 0x419794,
    }
    
    # Table entry formats
    TABLE_FORMATS = {
        'backgrounds': {
            'entry_size': 12,
            'format': '>III',  # [unknown, ram_ptr, rom_offset]
            'rom_offset_index': 2,
        },
        'characters': {
            'entry_size': 12,
            'format': '>III',  # [unknown, ram_ptr, rom_offset]
            'rom_offset_index': 2,
        },
        'music': {
            'entry_size': 8,
            'format': '>IHH',  # [cmp_offset, decomp_size, cmp_size]
            'rom_offset_index': 0,  # offset is relative to table base
        },
    }
    
    def __init__(self, rom_path: str, filetable_path: Optional[str] = None):
        """Initialize GE ROM driver"""
        self.rom_path = Path(rom_path)
        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM file not found: {self.rom_path}")
        
        # Load ROM data
        with open(self.rom_path, 'rb') as f:
            self.rom_data = bytearray(f.read())
        
        # Detect variant
        self.variant = self._detect_variant()
        
        # Try loading filetable.json if available
        self.filetable = self._load_filetable(filetable_path)
        
        # If no filetable, try to find tables in ROM or scan
        if not self.filetable:
            self.tables = self._find_tables()
            
            # If no tables found and scanner available, scan the ROM
            if not self.tables and ROMScanner:
                print("No filetable found and hardcoded tables not detected.")
                print("Scanning ROM for compressed files...")
                self._scan_rom()
        else:
            self.tables = {}
        
    def _detect_variant(self) -> str:
        """Detect ROM variant from header or filename"""
        # Check filename
        filename = self.rom_path.name.lower()
        if '(u)' in filename or '.u.' in filename:
            return 'ntsc'
        elif '(e)' in filename or '.e.' in filename:
            return 'pal'
        elif '(j)' in filename or '.j.' in filename:
            return 'jpn'
        
        # Could also check ROM header at 0x3E for region code
        # For now, assume NTSC
        return 'ntsc'
    
    def _load_filetable(self, filetable_path: Optional[str] = None) -> Optional[Dict]:
        """Load filetable.json if available"""
        if filetable_path:
            path = Path(filetable_path)
        else:
            # Try same directory as ROM
            path = self.rom_path.parent / "filetable.json"
        
        if path.exists():
            with open(path, 'r') as f:
                return json.load(f)
        
        # Try loading built-in vanilla GE filetable
        if self.variant == 'ntsc':
            builtin_path = Path(__file__).parent / 'data' / 'ge007_ntsc_filetable.json'
            if builtin_path.exists():
                # Check if this is a vanilla ROM by comparing size
                vanilla_size = 12582912  # 12MB vanilla GE NTSC ROM
                if len(self.rom_data) == vanilla_size:
                    print("Loading built-in vanilla GE NTSC filetable...")
                    with open(builtin_path, 'r') as f:
                        return json.load(f)
        
        return None
    
    def _find_tables(self) -> Dict:
        """Find file tables, trying vanilla offsets first then scanning"""
        tables = {}
        
        # Try vanilla offsets first
        vanilla_offsets = self.VANILLA_TABLES_NTSC
        
        for table_name, offset in vanilla_offsets.items():
            if self._validate_table(table_name, offset):
                tables[table_name] = {
                    'offset': offset,
                    'validated': True,
                    'entries': self._read_table(table_name, offset)
                }
            else:
                # Table not found at expected offset, try scanning
                scanned_offset = self._scan_for_table(table_name)
                if scanned_offset:
                    tables[table_name] = {
                        'offset': scanned_offset,
                        'validated': False,
                        'entries': self._read_table(table_name, scanned_offset)
                    }
        
        return tables
    
    def _validate_table(self, table_type: str, offset: int) -> bool:
        """Validate that offset points to a valid file table"""
        if offset + 48 > len(self.rom_data):
            return False
        
        try:
            # Read first 4 entries
            entries = self._read_table_entries(table_type, offset, count=4)
            
            # Get ROM offsets from entries
            rom_offsets = [e['rom_offset'] for e in entries if e['rom_offset'] != 0]
            
            if len(rom_offsets) < 2:
                return False
            
            # Check for ascending order
            if rom_offsets != sorted(rom_offsets):
                return False
            
            # Check if offsets point to valid ROM regions
            for rom_off in rom_offsets[:2]:  # Just check first 2
                if rom_off >= len(self.rom_data):
                    return False
                
                # Check for compression header or valid data
                header = struct.unpack('>H', self.rom_data[rom_off:rom_off+2])[0]
                if header not in [0x1172, 0x0000]:  # Compressed or uncompressed
                    return False
            
            return True
            
        except Exception:
            return False
    
    def _read_table_entries(self, table_type: str, offset: int, count: int = None) -> List[Dict]:
        """Read table entries from ROM"""
        fmt = self.TABLE_FORMATS[table_type]
        entry_size = fmt['entry_size']
        format_str = fmt['format']
        rom_offset_idx = fmt['rom_offset_index']
        
        entries = []
        pos = offset
        entry_num = 0
        
        while True:
            if count and entry_num >= count:
                break
            
            if pos + entry_size > len(self.rom_data):
                break
            
            # Read entry
            data = struct.unpack(format_str, self.rom_data[pos:pos+entry_size])
            
            # Parse based on table type
            if table_type in ['backgrounds', 'characters']:
                unknown, ram_ptr, rom_offset = data
                if rom_offset == 0:  # End of table marker
                    break
                entries.append({
                    'unknown': unknown,
                    'ram_ptr': ram_ptr,
                    'rom_offset': rom_offset,
                })
            elif table_type == 'music':
                cmp_offset, decomp_size, cmp_size = data
                if cmp_offset == 0:  # End of table marker
                    break
                # Music offset is relative to table base
                rom_offset = offset + cmp_offset
                entries.append({
                    'cmp_offset': cmp_offset,
                    'decomp_size': decomp_size,
                    'cmp_size': cmp_size,
                    'rom_offset': rom_offset,
                })
            
            pos += entry_size
            entry_num += 1
        
        return entries
    
    def _read_table(self, table_type: str, offset: int) -> List[Dict]:
        """Read complete table from ROM"""
        return self._read_table_entries(table_type, offset)
    
    def _scan_for_table(self, table_type: str) -> Optional[int]:
        """Scan ROM for file table patterns"""
        # TODO: Implement table scanning for patched ROMs
        # This would search for patterns of ascending ROM offsets
        return None
    
    def _scan_rom(self):
        """Scan ROM for files using ROMScanner"""
        scanner = ROMScanner(str(self.rom_path))
        scanner.scan(min_file_size=32, max_file_size=2*1024*1024, scan_uncompressed=True)
        
        # Generate filetable structure internally
        self.filetable = scanner.generate_filetable(output_path=None, rom_variant=self.variant)
        
        print(f"  Scan complete: {len(scanner.compressed_files)} compressed + {len(scanner.uncompressed_regions)} uncompressed = {len(scanner.compressed_files) + len(scanner.uncompressed_regions)} total files")
    
    def list_files(self, show_details=True) -> List[Dict]:
        """List all files found in ROM tables or filetable"""
        files = []
        file_id = 0
        
        if self.filetable:
            # Use filetable.json
            for file_entry in self.filetable.get('files', []):
                files.append({
                    'id': file_entry.get('id', file_id),
                    'filename': file_entry.get('name', f'file_{file_id:04d}'),
                    'category': file_entry.get('category', 'unknown'),
                    'rom_offset': int(file_entry['rom_offset'], 16) if isinstance(file_entry['rom_offset'], str) else file_entry['rom_offset'],
                    'compressed': file_entry.get('compressed', True),
                    'size': file_entry.get('size', 0),
                })
                file_id += 1
            
            if show_details:
                print(f"\n📋 GOLDENEYE FILE LISTING ({len(files)} files)")
                print("-" * 80)
                for i, f in enumerate(files[:20]):  # Show first 20
                    comp = 'Z' if f['compressed'] else ' '
                    cat = f['category'][:8].ljust(8)
                    size_str = self._format_size(f['size'])
                    print(f"{i:3d}  {comp}  {cat}  {size_str:>8}  {f['filename']}")
                if len(files) > 20:
                    print(f"... and {len(files) - 20} more files")
                print(f"\nTotal: {len(files)} files")
        else:
            # Use ROM tables
            for table_name, table_info in self.tables.items():
                for entry in table_info['entries']:
                    files.append({
                        'id': file_id,
                        'filename': f"{table_name}_{file_id:04d}",
                        'category': table_name,
                        'rom_offset': entry['rom_offset'],
                        'compressed': True,
                    })
                    file_id += 1
            
            if show_details:
                print(f"Found {len(files)} files in ROM tables")
        
        return files
    
    def _format_size(self, size: int) -> str:
        """Format size in human readable format"""
        if size < 1024:
            return f"{size}B"
        elif size < 1024 * 1024:
            return f"{size/1024:.1f}K"
        else:
            return f"{size/(1024*1024):.1f}M"
    
    def get_files(self) -> List[Dict]:
        """Get files list silently (no printing)"""
        return self.list_files(show_details=False)
    
    def decompress_file(self, rom_offset: int, size_hint: Optional[int] = None) -> bytes:
        """Decompress file at ROM offset"""
        if rom_offset >= len(self.rom_data):
            raise ValueError(f"ROM offset {rom_offset:08x} out of bounds")
        
        # Check compression header
        header = struct.unpack('>H', self.rom_data[rom_offset:rom_offset+2])[0]
        
        if header == 0x1172:
            # Goldeneye compression: no size prefix, just zlib data
            # Need to decompress until done or use size hint
            if size_hint:
                compressed_data = self.rom_data[rom_offset+2:rom_offset+2+size_hint]
            else:
                # Try decompressing, zlib will stop when done
                # Estimate max size
                max_size = min(len(self.rom_data) - rom_offset - 2, 1024 * 1024)
                compressed_data = self.rom_data[rom_offset+2:rom_offset+2+max_size]
            
            try:
                return zlib.decompress(compressed_data, wbits=-15)
            except zlib.error:
                # Try with different sizes
                return zlib.decompress(self.rom_data[rom_offset+2:], wbits=-15)
        
        elif header == 0x1173:
            # Perfect Dark style compression (shouldn't be in GE ROM)
            size_bytes = self.rom_data[rom_offset+2:rom_offset+5]
            size = struct.unpack('>I', b'\x00' + size_bytes)[0]
            compressed_data = self.rom_data[rom_offset+5:rom_offset+5+size]
            return zlib.decompress(compressed_data, wbits=-15)
        
        else:
            # Uncompressed data
            # Need size hint or scan for next file
            if size_hint:
                return bytes(self.rom_data[rom_offset:rom_offset+size_hint])
            else:
                # Return up to 1MB or end of ROM
                max_size = min(1024 * 1024, len(self.rom_data) - rom_offset)
                return bytes(self.rom_data[rom_offset:rom_offset+max_size])
    
    def extract_file(self, file_info: Dict, output_path: Path):
        """Extract a file to disk"""
        rom_offset = file_info['rom_offset']
        is_compressed = file_info.get('compressed', True)
        
        if is_compressed:
            # Get size hint if available
            size_hint = None
            if 'compressed_size' in file_info:
                size_hint = file_info['compressed_size']
            elif 'cmp_size' in file_info:
                size_hint = file_info['cmp_size']
            
            # Decompress
            data = self.decompress_file(rom_offset, size_hint)
        else:
            # Uncompressed file - just copy the data
            size = file_info.get('size', 0)
            if size == 0:
                # No size specified, this shouldn't happen but handle it
                size = min(1024 * 1024, len(self.rom_data) - rom_offset)
            data = bytes(self.rom_data[rom_offset:rom_offset+size])
        
        # Write to file
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(data)
        
        return len(data)
    
    def get_info(self) -> Dict:
        """Get ROM information"""
        if self.filetable:
            file_count = len(self.filetable.get('files', []))
            has_filetable = True
        else:
            file_count = sum(len(t['entries']) for t in self.tables.values())
            has_filetable = False
        
        info = {
            'path': str(self.rom_path),
            'size': len(self.rom_data),
            'variant': self.variant,
            'type': 'goldeneye',
            'file_count': file_count,
            'has_filetable': has_filetable,
        }
        
        if has_filetable:
            info['filetable_version'] = self.filetable.get('format_version', 'unknown')
            info['rom_type'] = self.filetable.get('rom_info', {}).get('type', 'unknown')
        else:
            info['tables'] = {
                name: {
                    'offset': f"0x{table_info['offset']:08x}",
                    'validated': table_info['validated'],
                    'count': len(table_info['entries']),
                }
                for name, table_info in self.tables.items()
            }
        
        return info


def test_ge_rom(rom_path: str):
    """Test GE ROM driver"""
    print(f"Testing GE ROM: {rom_path}")
    driver = GEROMDriver(rom_path)
    
    info = driver.get_info()
    print(f"\nROM Info:")
    print(f"  Type: {info['type']}")
    print(f"  Variant: {info['variant']}")
    print(f"  Size: {info['size'] / (1024*1024):.1f}M")
    print(f"  Files: {info['file_count']}")
    print(f"  Using filetable: {'Yes' if info['has_filetable'] else 'No'}")
    
    if 'tables' in info:
        print(f"\nTables:")
        for name, table_info in info['tables'].items():
            status = "✓" if table_info['validated'] else "?"
            print(f"  {status} {name}: {table_info['offset']} ({table_info['count']} entries)")
    
    # List first few files
    files = driver.list_files()
    print(f"\nFirst 10 files:")
    for f in files[:10]:
        compressed = "compressed" if f.get('compressed', False) else "raw"
        print(f"  [{f['id']:4d}] {f['name']:50s} @ 0x{f['rom_offset']:08x} ({compressed})")


if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        test_ge_rom(sys.argv[1])
    else:
        print("Usage: ge_rom_driver.py <rom_path>")
