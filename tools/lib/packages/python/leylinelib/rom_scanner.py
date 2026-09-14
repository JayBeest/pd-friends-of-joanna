#!/usr/bin/env python3
"""
ROM File Table Scanner
=====================

Scans N64 ROMs to detect compressed files and generate filetable.json
for patched ROMs where hardcoded table offsets don't work.

Approach:
1. Scan for compression headers (0x1172 for GE, 0x1173 for PD)
2. Validate compressed data integrity
3. Detect file boundaries
4. Generate filetable.json structure
"""

import struct
import zlib
import hashlib
import csv
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import json


class ROMScanner:
    """Generic N64 ROM scanner for file detection"""
    
    # Known compression headers
    COMPRESSION_HEADERS = {
        0x1172: 'goldeneye',   # GE: no size prefix
        0x1173: 'perfect_dark', # PD: 3-byte size prefix
    }
    
    def __init__(self, rom_path: str, symbol_file: Optional[str] = None, 
                 filelist_csv: Optional[str] = None):
        """
        Initialize ROM scanner
        
        Args:
            rom_path: Path to ROM file
            symbol_file: Optional path to symbol JSON file for address lookups
            filelist_csv: Optional path to decomp filelist CSV for known file names
        """
        self.rom_path = Path(rom_path)
        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM file not found: {rom_path}")
        
        with open(self.rom_path, 'rb') as f:
            self.rom_data = bytearray(f.read())
        
        self.rom_type = None
        self.compressed_files = []
        self.uncompressed_regions = []
        
        # Load symbols if provided
        self.symbols = None
        if symbol_file:
            try:
                from leylinelib.symbol_extractor import SymbolExtractor
                self.symbols = SymbolExtractor.load_symbols(symbol_file)
                print(f"Loaded {len(self.symbols)} symbols from {symbol_file}")
            except Exception as e:
                print(f"Warning: Could not load symbols: {e}")
                self.symbols = None
        
        # Load decomp filelist CSV if provided
        self.filelist = None
        if filelist_csv:
            self.filelist = self._load_filelist_csv(filelist_csv)
    
    def scan(self, min_file_size: int = 32, max_file_size: int = 2 * 1024 * 1024, 
             scan_uncompressed: bool = True) -> Dict:
        """
        Scan ROM for files
        
        Args:
            min_file_size: Minimum file size to consider (bytes)
            max_file_size: Maximum file size to consider (bytes)
            scan_uncompressed: Whether to scan for uncompressed files
        
        Returns:
            Dictionary with scan results
        """
        print(f"Scanning ROM: {self.rom_path.name} ({len(self.rom_data) / (1024*1024):.1f}M)")
        
        # Step 1: Find all compression headers
        self._find_compressed_files(min_file_size, max_file_size)
        
        # Step 2: Detect ROM type from compression headers
        self._detect_rom_type()
        
        # Step 3: Find uncompressed regions
        if scan_uncompressed:
            self._find_uncompressed_regions(min_file_size)
        
        return {
            'rom_type': self.rom_type,
            'compressed_files': len(self.compressed_files),
            'uncompressed_regions': len(self.uncompressed_regions),
            'total_files': len(self.compressed_files) + len(self.uncompressed_regions),
        }
    
    def _find_compressed_files(self, min_size: int, max_size: int):
        """Find all compressed files in ROM"""
        print("Scanning for compressed files...")
        
        candidates = []
        
        # Scan for compression headers
        for offset in range(0, len(self.rom_data) - 2, 4):
            header = struct.unpack('>H', self.rom_data[offset:offset+2])[0]
            
            if header in self.COMPRESSION_HEADERS:
                # Try to decompress and validate
                file_info = self._validate_compressed_file(offset, header, max_size)
                if file_info:
                    candidates.append(file_info)
        
        # Sort by offset
        candidates.sort(key=lambda x: x['rom_offset'])
        
        # Remove overlapping files (keep the one that validates better)
        self.compressed_files = self._remove_overlaps(candidates)
        
        print(f"  Found {len(self.compressed_files)} valid compressed files")
    
    def _validate_compressed_file(self, offset: int, header: int, max_size: int) -> Optional[Dict]:
        """Validate that offset contains a real compressed file"""
        compression_type = self.COMPRESSION_HEADERS[header]
        
        try:
            if compression_type == 'goldeneye':
                # GE: header + raw zlib data
                # Try to decompress with various sizes
                for test_size in [512, 1024, 4096, 16384, 65536, max_size]:
                    if offset + 2 + test_size > len(self.rom_data):
                        test_size = len(self.rom_data) - offset - 2
                    
                    try:
                        compressed_data = self.rom_data[offset+2:offset+2+test_size]
                        decompressed = zlib.decompress(compressed_data, wbits=-15)
                        
                        # Successfully decompressed
                        # Estimate actual compressed size by finding zlib end
                        actual_cmp_size = self._find_zlib_end(offset + 2)
                        
                        return {
                            'rom_offset': offset,
                            'header': header,
                            'compression_type': compression_type,
                            'compressed_size': actual_cmp_size,
                            'decompressed_size': len(decompressed),
                            'validated': True,
                        }
                    except zlib.error:
                        continue
                
            elif compression_type == 'perfect_dark':
                # PD: header + 3-byte size + zlib data
                if offset + 5 > len(self.rom_data):
                    return None
                
                size_bytes = self.rom_data[offset+2:offset+5]
                cmp_size = struct.unpack('>I', b'\x00' + size_bytes)[0]
                
                if cmp_size < 8 or cmp_size > max_size:
                    return None
                
                if offset + 5 + cmp_size > len(self.rom_data):
                    return None
                
                compressed_data = self.rom_data[offset+5:offset+5+cmp_size]
                decompressed = zlib.decompress(compressed_data, wbits=-15)
                
                return {
                    'rom_offset': offset,
                    'header': header,
                    'compression_type': compression_type,
                    'compressed_size': cmp_size,
                    'decompressed_size': len(decompressed),
                    'validated': True,
                }
        
        except Exception:
            pass
        
        return None
    
    def _find_zlib_end(self, start_offset: int) -> int:
        """Find the end of a zlib stream by decompressing"""
        # Create decompressor
        decomp = zlib.decompressobj(wbits=-15)
        
        chunk_size = 1024
        offset = start_offset
        total_consumed = 0
        
        while offset < len(self.rom_data):
            chunk = bytes(self.rom_data[offset:offset+chunk_size])
            try:
                decomp.decompress(chunk)
                total_consumed += len(chunk)
                offset += chunk_size
                
                # Check if decompression is complete
                if decomp.eof:
                    # Find exact end by checking unused_data
                    if decomp.unused_data:
                        total_consumed -= len(decomp.unused_data)
                    return total_consumed
            except zlib.error:
                # End of stream or error
                break
        
        return total_consumed
    
    def _detect_rom_type(self):
        """Detect ROM type from compression headers found"""
        if not self.compressed_files:
            self.rom_type = 'unknown'
            return
        
        # Count compression types
        types = {}
        for file_info in self.compressed_files:
            ctype = file_info['compression_type']
            types[ctype] = types.get(ctype, 0) + 1
        
        # Use most common type
        if types:
            self.rom_type = max(types.items(), key=lambda x: x[1])[0]
        else:
            self.rom_type = 'unknown'
        
        print(f"  Detected ROM type: {self.rom_type}")
    
    def _find_uncompressed_regions(self, min_size: int = 32):
        """
        Find regions that might be uncompressed files
        
        Strategy:
        1. Build a map of all compressed file locations
        2. Find gaps between compressed files
        3. Analyze gaps for data vs padding
        4. Identify likely file boundaries using alignment
        """
        print("Scanning for uncompressed files...")
        
        # Build occupied regions map
        occupied = []
        for cfile in self.compressed_files:
            offset = cfile['rom_offset']
            # For GE: header(2) + compressed_size
            # For PD: header(2) + size_field(3) + compressed_size
            if cfile['compression_type'] == 'goldeneye':
                size = 2 + cfile.get('compressed_size', 0)
            else:  # perfect_dark
                size = 5 + cfile.get('compressed_size', 0)
            occupied.append((offset, offset + size))
        
        # Sort by start offset
        occupied.sort()
        
        # Find gaps
        gaps = []
        last_end = 0
        
        for start, end in occupied:
            if start > last_end:
                gap_size = start - last_end
                if gap_size >= min_size:
                    gaps.append({
                        'start': last_end,
                        'end': start,
                        'size': gap_size,
                    })
            last_end = max(last_end, end)
        
        # Add final gap to end of ROM
        if last_end < len(self.rom_data):
            gaps.append({
                'start': last_end,
                'end': len(self.rom_data),
                'size': len(self.rom_data) - last_end,
            })
        
        print(f"  Found {len(gaps)} gaps between compressed files")
        
        # Analyze each gap for uncompressed files
        candidates = []
        for gap in gaps:
            files = self._analyze_gap(gap, min_size)
            candidates.extend(files)
        
        self.uncompressed_regions = candidates
        print(f"  Found {len(self.uncompressed_regions)} uncompressed regions")
    
    def _analyze_gap(self, gap: Dict, min_size: int) -> List[Dict]:
        """
        Analyze a gap to find uncompressed files
        
        Heuristics:
        - Look for aligned boundaries (0x10, 0x80, 0x100, 0x1000)
        - Detect padding (0x00, 0xFF runs)
        - Check data entropy (real data vs padding)
        - Respect common N64 alignment patterns
        """
        start = gap['start']
        end = gap['end']
        gap_size = gap['size']
        
        # Skip very small gaps
        if gap_size < min_size:
            return []
        
        # Skip gaps that are mostly padding
        if self._is_mostly_padding(start, min(1024, gap_size)):
            return []
        
        # Try to split gap into files based on alignment
        files = []
        
        # Common N64 alignments (from most to least common)
        alignments = [0x1000, 0x800, 0x100, 0x80, 0x10]
        
        # Find alignment boundaries within gap
        boundaries = [start]
        
        for alignment in alignments:
            offset = ((start + alignment - 1) // alignment) * alignment
            while offset < end:
                if offset > start and offset not in boundaries:
                    # Check if this looks like a file boundary
                    if self._is_file_boundary(offset):
                        boundaries.append(offset)
                offset += alignment
        
        boundaries.append(end)
        boundaries.sort()
        
        # Create files from boundaries
        for i in range(len(boundaries) - 1):
            file_start = boundaries[i]
            file_end = boundaries[i + 1]
            file_size = file_end - file_start
            
            if file_size >= min_size:
                # Check if this region has data
                if not self._is_mostly_padding(file_start, min(1024, file_size)):
                    files.append({
                        'rom_offset': file_start,
                        'size': file_size,
                        'compressed': False,
                        'type': 'uncompressed',
                    })
        
        return files
    
    def _is_mostly_padding(self, offset: int, check_size: int) -> bool:
        """Check if a region is mostly padding (0x00 or 0xFF)"""
        if offset + check_size > len(self.rom_data):
            check_size = len(self.rom_data) - offset
        
        if check_size == 0:
            return True
        
        data = self.rom_data[offset:offset+check_size]
        
        # Count 0x00 and 0xFF bytes
        zero_count = data.count(0x00)
        ff_count = data.count(0xFF)
        
        # If > 90% is padding, consider it padding
        padding_ratio = (zero_count + ff_count) / check_size
        return padding_ratio > 0.9
    
    def _is_file_boundary(self, offset: int) -> bool:
        """
        Check if offset looks like a file boundary
        
        Heuristics:
        - Preceded by padding
        - Followed by non-padding data
        - Has reasonable data patterns
        """
        if offset < 0x100 or offset >= len(self.rom_data) - 0x100:
            return False
        
        # Check 256 bytes before (should be padding or end of file)
        before_is_padding = self._is_mostly_padding(offset - 256, 256)
        
        # Check 256 bytes after (should be data)
        after_is_data = not self._is_mostly_padding(offset, 256)
        
        return before_is_padding and after_is_data
    
    def _remove_overlaps(self, candidates: List[Dict]) -> List[Dict]:
        """Remove overlapping file candidates"""
        if not candidates:
            return []
        
        non_overlapping = []
        
        for candidate in candidates:
            offset = candidate['rom_offset']
            size = candidate.get('compressed_size', 0) + 2  # +2 for header
            
            # Check if this overlaps with any accepted file
            overlaps = False
            for accepted in non_overlapping:
                acc_offset = accepted['rom_offset']
                acc_size = accepted.get('compressed_size', 0) + 2
                
                # Check overlap
                if offset < acc_offset + acc_size and offset + size > acc_offset:
                    overlaps = True
                    break
            
            if not overlaps:
                non_overlapping.append(candidate)
        
        return non_overlapping
    
    def _load_filelist_csv(self, csv_path: str) -> Dict[int, Dict]:
        """
        Load decomp filelist CSV and create offset->file mapping.
        
        CSV format: rom_offset,size,path,compressed,?
        
        Args:
            csv_path: Path to filelist CSV
            
        Returns:
            Dict mapping ROM offset to file info
        """
        filelist = {}
        try:
            with open(csv_path, 'r') as f:
                reader = csv.reader(f)
                for row in reader:
                    if len(row) >= 3:
                        offset = int(row[0])
                        size = int(row[1])
                        path = row[2]
                        compressed = int(row[3]) if len(row) > 3 else 0
                        
                        # Extract filename and category from path
                        path_parts = Path(path).parts
                        filename = Path(path).name
                        
                        # Determine category from path
                        category = 'unknown'
                        if 'obseg' in path_parts:
                            if 'chr' in path_parts:
                                category = 'chrs'
                            elif 'gun' in path_parts:
                                category = 'guns'
                            elif 'prop' in path_parts:
                                category = 'props'
                            elif 'setup' in path_parts:
                                category = 'setup'
                            elif 'bg' in path_parts:
                                category = 'bgdata'
                            elif 'text' in path_parts:
                                category = 'lang'
                            elif 'stan' in path_parts:
                                category = 'stan'
                            elif 'brief' in path_parts:
                                category = 'brief'
                        elif 'bgdata' in path_parts:
                            category = 'bgdata'
                        elif 'music' in path_parts or 'sfx' in filename:
                            category = 'audio'
                        elif 'font' in path_parts:
                            category = 'font'
                        elif 'ramrom' in path_parts:
                            category = 'ramrom'
                        elif 'images' in path_parts:
                            category = 'images'
                        
                        filelist[offset] = {
                            'name': filename,
                            'path': path,
                            'size': size,
                            'category': category,
                            'compressed': bool(compressed),
                        }
            
            print(f"Loaded {len(filelist)} files from decomp CSV: {csv_path}")
        except Exception as e:
            print(f"Warning: Could not load filelist CSV: {e}")
            return {}
        
        return filelist
    
    def _extract_compressed_file(self, file_info: Dict) -> Optional[bytes]:
        """Extract and decompress a file from ROM"""
        try:
            offset = file_info['rom_offset']
            compression_type = file_info.get('compression_type')
            
            if compression_type == 'goldeneye':
                # GE: header + zlib data
                cmp_size = file_info.get('compressed_size', 0)
                compressed_data = self.rom_data[offset+2:offset+2+cmp_size]
                return zlib.decompress(compressed_data, wbits=-15)
                
            elif compression_type == 'perfect_dark':
                # PD: header + 3-byte size + zlib data  
                cmp_size = file_info.get('compressed_size', 0)
                compressed_data = self.rom_data[offset+5:offset+5+cmp_size]
                return zlib.decompress(compressed_data, wbits=-15)
        except Exception as e:
            print(f"Warning: Failed to extract file at 0x{offset:08x}: {e}")
            return None
    
    def generate_filetable(self, output_path: Optional[str] = None, rom_variant: str = 'unknown',
                          include_hashes: bool = False) -> Dict:
        """
        Generate filetable.json structure from scan results
        
        Args:
            output_path: Path to write filetable.json (optional)
            rom_variant: ROM variant name (ntsc, pal, jpn, etc.)
            include_hashes: Whether to compute and include SHA256 hashes for decompressed data
        
        Returns:
            Filetable dictionary
        """
        files = []
        file_id = 0
        
        # Add compressed files
        for file_info in self.compressed_files:
            rom_offset = file_info['rom_offset']
            
            # Try to find name from decomp filelist first (most reliable)
            name = None
            category = 'scanned_compressed'
            
            if self.filelist and rom_offset in self.filelist:
                csv_entry = self.filelist[rom_offset]
                name = csv_entry['name']
                category = csv_entry['category']
            elif self.symbols:
                # Fallback to symbol lookup (less reliable for ROM offsets)
                from leylinelib.symbol_extractor import SymbolExtractor
                symbol_name = SymbolExtractor.lookup_address(
                    self.symbols, 
                    rom_offset
                )
                if symbol_name:
                    # Clean up symbol name for filename
                    clean_name = symbol_name.replace('/', '_').replace(' ', '_')
                    name = f"{clean_name}.bin"
            
            # Default name if no match found
            if not name:
                name = f"compressed_{file_id:04d}.bin"
            
            file_entry = {
                'id': file_id,
                'name': name,
                'category': category,
                'rom_offset': f"0x{rom_offset:08x}",
                'compressed_size': file_info.get('compressed_size', 0),
                'decompressed_size': file_info.get('decompressed_size', 0),
                'compressed': True,
                'compression_type': file_info.get('compression_type', 'unknown'),
            }
            
            # Add hash if requested
            if include_hashes:
                decompressed_data = self._extract_compressed_file(file_info)
                if decompressed_data:
                    file_entry['sha256'] = hashlib.sha256(decompressed_data).hexdigest()
            
            files.append(file_entry)
            file_id += 1
        
        # Add uncompressed files
        for file_info in self.uncompressed_regions:
            rom_offset = file_info['rom_offset']
            
            # Try to find name from decomp filelist first
            name = None
            category = 'scanned_uncompressed'
            
            if self.filelist and rom_offset in self.filelist:
                csv_entry = self.filelist[rom_offset]
                name = csv_entry['name']
                category = csv_entry['category']
            elif self.symbols:
                from leylinelib.symbol_extractor import SymbolExtractor
                symbol_name = SymbolExtractor.lookup_address(
                    self.symbols,
                    rom_offset
                )
                if symbol_name:
                    clean_name = symbol_name.replace('/', '_').replace(' ', '_')
                    name = f"{clean_name}.bin"
            
            if not name:
                name = f"uncompressed_{file_id:04d}.bin"
            
            file_entry = {
                'id': file_id,
                'name': name,
                'category': category,
                'rom_offset': f"0x{rom_offset:08x}",
                'size': file_info.get('size', 0),
                'compressed': False,
            }
            
            # Add hash if requested
            if include_hashes:
                offset = file_info['rom_offset']
                size = file_info.get('size', 0)
                data = bytes(self.rom_data[offset:offset+size])
                file_entry['sha256'] = hashlib.sha256(data).hexdigest()
            
            files.append(file_entry)
            file_id += 1
        
        filetable = {
            'format_version': '2.0',
            'rom_info': {
                'type': self.rom_type,
                'variant': rom_variant,
                'scanned': True,
                'rom_size': len(self.rom_data),
                'rom_file': self.rom_path.name,
                'symbols_loaded': self.symbols is not None,
            },
            'scan_info': {
                'compressed_files': len(self.compressed_files),
                'uncompressed_files': len(self.uncompressed_regions),
                'total_files': len(files),
            },
            'files': files,
        }
        
        if output_path:
            output_path = Path(output_path)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w') as f:
                json.dump(filetable, f, indent=2)
            print(f"\nWrote filetable.json: {output_path}")
            print(f"  Compressed: {len(self.compressed_files)}")
            print(f"  Uncompressed: {len(self.uncompressed_regions)}")
            print(f"  Total: {len(files)}")
            if self.symbols:
                print(f"  Symbols loaded: {len(self.symbols)}")
        
        return filetable
    
    def print_summary(self):
        """Print scan summary"""
        print(f"\n{'='*60}")
        print(f"ROM Scan Summary")
        print(f"{'='*60}")
        print(f"ROM: {self.rom_path.name}")
        print(f"Size: {len(self.rom_data) / (1024*1024):.1f}M")
        print(f"Type: {self.rom_type}")
        print(f"\nFiles Found:")
        print(f"  Compressed: {len(self.compressed_files)}")
        print(f"  Uncompressed: {len(self.uncompressed_regions)}")
        print(f"  Total: {len(self.compressed_files) + len(self.uncompressed_regions)}")
        
        if self.compressed_files:
            print(f"\nFirst 10 compressed files:")
            for i, file_info in enumerate(self.compressed_files[:10]):
                offset = file_info['rom_offset']
                cmp_size = file_info.get('compressed_size', 0)
                dec_size = file_info.get('decompressed_size', 0)
                print(f"  [{i:4d}] 0x{offset:08x}  cmp:{cmp_size:7d}  dec:{dec_size:7d}")


def scan_rom(rom_path: str, output_filetable: Optional[str] = None, variant: str = 'unknown',
             symbol_file: Optional[str] = None, include_hashes: bool = False,
             filelist_csv: Optional[str] = None) -> Dict:
    """
    Scan a ROM and optionally generate filetable.json
    
    Args:
        rom_path: Path to ROM file
        output_filetable: Path to output filetable.json (optional)
        variant: ROM variant (ntsc, pal, jpn)
        symbol_file: Optional path to symbol JSON file for address lookups
        include_hashes: Whether to compute SHA256 hashes for all files (slower but enables comparison)
        filelist_csv: Optional path to decomp filelist CSV for known file names
    
    Returns:
        Filetable dictionary
    """
    scanner = ROMScanner(rom_path, symbol_file=symbol_file, filelist_csv=filelist_csv)
    scanner.scan()
    scanner.print_summary()
    
    filetable = scanner.generate_filetable(output_filetable, variant, include_hashes=include_hashes)
    
    return filetable


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: rom_scanner.py <rom_path> [output_filetable.json] [variant] [--hashes] [--filelist <csv>]")
        print("\nExample:")
        print("  rom_scanner.py ge007.u.z64 filetable.json ntsc --hashes --filelist scripts/filelist.u.csv")
        print("\nOptions:")
        print("  --hashes           Compute SHA256 hashes for all files (enables file comparison)")
        print("  --filelist <csv>   Use decomp filelist CSV for known file names and categories")
        sys.exit(1)
    
    rom_path = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else None
    variant = 'unknown'
    include_hashes = False
    filelist_csv = None
    
    # Parse remaining args
    i = 2
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == '--hashes':
            include_hashes = True
            i += 1
        elif arg == '--filelist':
            if i + 1 < len(sys.argv):
                filelist_csv = sys.argv[i + 1]
                i += 2
            else:
                print("Error: --filelist requires a path argument")
                sys.exit(1)
        elif not arg.startswith('--') and variant == 'unknown':
            variant = arg
            i += 1
        else:
            i += 1
    
    scan_rom(rom_path, output, variant, include_hashes=include_hashes, filelist_csv=filelist_csv)
