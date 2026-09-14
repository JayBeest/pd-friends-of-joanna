#!/usr/bin/env python3
"""
Perfect Dark ROM Driver
Based on the original pd-extract tool from the n64 branch

Provides comprehensive ROM file extraction with proper file table parsing,
compression handling, and file categorization for any Perfect Dark ROM variant.

Attribution
-----------
Derived from Ryan Dwyer's pd-extract scripts (perfect-dark decomp
tooling). The segment-offset table, file-table parsing, and per-ROM-
variant handling are direct ports of his work; bugs in this port are
ours, not his.
"""

import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent))

import os
import zlib
import struct
import json
from rompath import ROMPath

class PDROMDriver:
    """
    Perfect Dark ROM driver based on the original pd-extract tool
    Handles all ROM variants with proper segment offsets and file extraction
    """
    
    # Segment offset table from original pd-extract tool
    # Format: [ntsc-beta, ntsc-1.0, ntsc-final, pal-beta, pal-final, jpn-final]
    SEGMENT_OFFSETS = {
        'files':           [0x29160,   0x28080,   0x28080,   0x29b90,   0x28910,   0x28800],
        'data':            [0x30850,   0x39850,   0x39850,   0x39850,   0x39850,   0x39850],
        'game':            [0x43c40,   0x4fc40,   0x4fc40,   0x4fc40,   0x4fc40,   0x4fc40],
        'jpnfontsingle':   [0x148c40,  0x194b20,  0x194b20,  0x180330,  0x180330,  0],
        'jpnfontmulti':    [0x154340,  0x19fb40,  0x19fb40,  0x18b340,  0x18b340,  0],
        'jpnfontcombined': [0,         0,         0,         0,         0,         0x179330],
        'animations':      [0x155dc0,  0x1a15c0,  0x1a15c0,  0x18cdc0,  0x18cdc0,  0x190c50],
        'mpconfigs':       [0x785130,  0x7d0a40,  0x7d0a40,  0x7bc240,  0x7bc240,  0x7c00d0],
        'firingrange':     [0x79e410,  0x7e9d20,  0x7e9d20,  0x7d5520,  0x7d5520,  0x7d93b0],
        'textureconfig':   [0x79f960,  0x7eb270,  0x7eb270,  0x7d6a70,  0x7d6a70,  0x7da900],
        'textures':        [0x1d12fe0, 0x1d65f40, 0x1d65f40, 0x1d5bb50, 0x1d5ca20, 0x1d61f90],
        'copyright':       [0x1fabac0, 0x1ffea20, 0x1ffea20, 0x1ff4630, 0x1ff5500, 0x1ffd6b0],
    }
    
    ROM_VARIANTS = ['ntsc-beta', 'ntsc-1.0', 'ntsc-final', 'pal-beta', 'pal-final', 'jpn-final']
    
    def __init__(self, rom_path="pd.ntsc-final.z64"):
        self.rom_path = Path(rom_path)
        self.rom_data = None
        self.decompressed_data = None
        self.rom_variant = None
        self._load_rom()
    
    def _load_rom(self):
        """Load ROM data and detect variant"""
        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM file not found: {self.rom_path}")
        
        with open(self.rom_path, 'rb') as f:
            self.rom_data = f.read()

        # Detect ROM variant from filename first (cheap, exact when ROM
        # is named conventionally).
        filename = self.rom_path.name.lower()
        for variant in self.ROM_VARIANTS:
            if variant in filename:
                self.rom_variant = variant
                break

        # Filename detection misses for modded / rebranded ROMs (e.g.
        # `gex.z64`). Fall back to content-based detection by trying
        # each variant's data segment offset and picking the one whose
        # 0x1173 segment decompresses cleanly.
        if not self.rom_variant:
            self.rom_variant = self._detect_variant_by_content()
            if self.rom_variant:
                print(f"ROM variant detected by content: {self.rom_variant}", file=sys.stderr)
            else:
                self.rom_variant = 'ntsc-final'
                print(f"Warning: Could not detect ROM variant; assuming {self.rom_variant}", file=sys.stderr)
        else:
            print(f"ROM variant detected: {self.rom_variant}", file=sys.stderr)

        # Decompress the data segment
        data_offset = self._get_segment_offset('data')
        self.decompressed_data = self._decompress_segment(self.rom_data[data_offset:])

    def _detect_variant_by_content(self):
        """Probe each variant's data offset for a valid 0x1173 segment.

        Returns the variant name whose canonical data offset starts
        with the PD compression magic AND decompresses without error,
        or None if no variant fits.
        """
        candidates = []
        for variant in self.ROM_VARIANTS:
            idx = self.ROM_VARIANTS.index(variant)
            off = self.SEGMENT_OFFSETS['data'][idx]
            if off + 5 > len(self.rom_data):
                continue
            if self.rom_data[off:off + 2] != b'\x11\x73':
                continue
            try:
                decomp = zlib.decompress(self.rom_data[off + 5:], wbits=-15)
            except zlib.error:
                continue
            candidates.append((variant, len(decomp)))
        if not candidates:
            return None
        # Prefer the largest decompressed segment (modded ROMs tend to
        # match multiple offsets coincidentally; the real one almost
        # always yields the most data).
        candidates.sort(key=lambda c: c[1], reverse=True)
        return candidates[0][0]
    
    def _get_segment_offset(self, segment_name):
        """Get segment offset for current ROM variant"""
        if segment_name not in self.SEGMENT_OFFSETS:
            raise ValueError(f"Unknown segment: {segment_name}")
        
        variant_index = self.ROM_VARIANTS.index(self.rom_variant)
        return self.SEGMENT_OFFSETS[segment_name][variant_index]
    
    def _decompress_segment(self, data):
        """Decompress segment data using Perfect Dark compression format"""
        if len(data) < 5:
            return data
            
        # Check for Perfect Dark compression header (0x1173)
        header = struct.unpack('>H', data[0:2])[0]
        if header == 0x1173:
            return zlib.decompress(data[5:], wbits=-15)
        else:
            return data
    
    def _decompress_with_unused(self, data):
        """Decompress and return unused data for proper file boundary detection"""
        if len(data) < 5:
            return data, b''
            
        header = struct.unpack('>H', data[0:2])[0]
        if header == 0x1173:
            obj = zlib.decompressobj(wbits=-15)
            decompressed = obj.decompress(data[5:])
            return decompressed, obj.unused_data
        else:
            return data, b''
    
    def get_file_offsets(self):
        """Extract file offsets from the file table (same logic as original)"""
        # The file offsets are stored in the decompressed data segment
        # Starting from the files offset within the decompressed data
        files_offset_in_decompressed = self._get_segment_offset('files')
        offsets = []
        offset_pos = files_offset_in_decompressed
        
        while offset_pos + 4 <= len(self.decompressed_data):
            offset = struct.unpack('>I', self.decompressed_data[offset_pos:offset_pos+4])[0]
            if offset == 0 and len(offsets) > 0:
                break
            # These are actual ROM file offsets
            offsets.append(offset)
            offset_pos += 4
        
        return offsets
    
    def get_file_names(self, name_table_offset):
        """Extract file names from the name table (same logic as original)"""
        names = []
        offset = 0
        
        while offset + 4 <= len(self.rom_data) - name_table_offset:
            name_offset = struct.unpack('>I', self.rom_data[name_table_offset + offset:name_table_offset + offset + 4])[0]
            if name_offset == 0 and len(names) > 0:
                break
            
            # Read null-terminated string
            name_addr = name_table_offset + name_offset
            name_end = self.rom_data[name_addr:].find(0)
            if name_end == -1:
                break
            
            name = self.rom_data[name_addr:name_addr + name_end].decode('utf-8', errors='ignore')
            names.append(name)
            offset += 4
        
        return names
    
    def extract_files(self, quiet: bool = False):
        """Extract all files from ROM using original pd-extract logic"""
        if not quiet:
            print(f"🎮 EXTRACTING FILES FROM {self.rom_variant.upper()} ROM")
            print("=" * 60)
        
        # Get file offsets
        offsets = self.get_file_offsets()
        if not offsets:
            print("No file offsets found")
            return []
        
        if not quiet:
            print(f"Found {len(offsets)} file offset entries")
        
        # Get file names from the last offset (name table location)
        names = self.get_file_names(offsets[-1])
        if not quiet:
            print(f"Found {len(names)} file names")
        
        files_extracted = []
        
        for index, offset in enumerate(offsets[:-1]):  # Skip last entry (name table)
            if index >= len(names):
                break
                
            try:
                end_offset = offsets[index + 1]
            except IndexError:
                continue
            
            content = self.rom_data[offset:end_offset]
            name = names[index]

            # Handle compression. A modded ROM may contain a single
            # malformed 0x1173 blob (observed: GE-X `Ump_setupearZ`).
            # Skip-on-error so one bad file doesn't abort the whole
            # extraction.
            unzipped = None
            if len(content) >= 2 and content[0:2] == b'\x11\x73':
                try:
                    unzipped, unused = self._decompress_with_unused(content)
                    if len(unused) > 0:
                        content = content[:-len(unused)]
                except zlib.error as e:
                    if not quiet:
                        print(f"Warning: failed to decompress {name!r} "
                              f"at 0x{offset:x}: {e}; keeping raw bytes")
                    unzipped = None
            
            # Determine unzipped name
            unzipped_name = name[:-1] if name.endswith('Z') else name
            
            # Categorize file (same logic as original)
            file_info = {
                'index': index,
                'name': name,
                'unzipped_name': unzipped_name,
                'offset': offset,
                'size': len(content),
                'end_offset': end_offset,
                'compressed': unzipped is not None,
                'uncompressed_size': len(unzipped) if unzipped else len(content),
                'content': content,
                'unzipped_content': unzipped,
                'category': self._categorize_file(name),
                'subcategory': self._get_subcategory(name)
            }
            
            files_extracted.append(file_info)
        
        return files_extracted
    
    def _categorize_file(self, name):
        """Categorize file based on name prefix (same logic as original)"""
        # Handle objects first (ob/ prefix files)
        if name.startswith('ob/'):
            return 'objects'
        elif name.endswith('.seg'):
            return 'bgdata'
        elif name.endswith('padsZ') or name.endswith('tilesZ'):
            return 'bgdata'
        elif name.startswith('A'):
            return 'audio'
        elif name.startswith('C'):
            return 'chrs'
        elif name.startswith('G'):
            return 'guns'
        elif name.startswith('L'):
            return 'lang'
        elif name.startswith('P'):
            return 'props'
        elif name.startswith('U'):
            return 'setup'
        else:
            return 'unknown'
    
    def _get_subcategory(self, name):
        """Get subcategory for better file organization"""
        if name.endswith('padsZ'):
            return 'pads'
        elif name.endswith('tilesZ'):
            return 'tiles'
        elif name.endswith('.seg'):
            return 'segments'
        else:
            return None
    
    def get_files(self):
        """Get list of all files without printing (for internal use)"""
        # Temporarily suppress stdout to make this truly silent
        import io
        import sys
        old_stdout = sys.stdout
        sys.stdout = io.StringIO()
        try:
            files = self.extract_files()
            return files
        finally:
            sys.stdout = old_stdout
    
    def get_files_info(self):
        # f  = self.get_files()
        # breakpoint()
        return self.list_files(show_details=False, show_json=True, get=True)
    
    def list_files(self, show_details=True, show_json=False, get=False):
        """List all files in the ROM with details"""
        files = self.extract_files(quiet=True)


        def listfiles_showdetails():
            # Detailed listing
            print(f"\n📋 DETAILED FILE LISTING ({len(files)} files)")
            print("-" * 80)
            print("Idx Type     Category  Size      Uncomp    Name")
            print("-" * 80)
            
            total_compressed = 0
            total_uncompressed = 0
            categories = {}
            
            for file_info in files:
                comp_indicator = 'Z' if file_info['compressed'] else ' '
                cat = file_info['category'][:8].ljust(8)
                size = self._format_size(file_info['size'])
                uncomp_size = self._format_size(file_info['uncompressed_size'])
                
                # Track statistics
                total_compressed += file_info['size']
                total_uncompressed += file_info['uncompressed_size']
                categories[file_info['category']] = categories.get(file_info['category'], 0) + 1
                
                print(f"{file_info['index']:3d}  {comp_indicator}    {cat} {size:>8}  {uncomp_size:>8}  {file_info['name']}")
            
            print("-" * 80)
            print(f"Total: {len(files)} files")
            print(f"Compressed data: {self._format_size(total_compressed)}")
            print(f"Uncompressed data: {self._format_size(total_uncompressed)}")
            
            print(f"\n📊 CATEGORY BREAKDOWN:")
            for category, count in sorted(categories.items()):
                print(f"  {category:10}: {count:3d} files")
        def listfiles_nodetails():
            # Simple listing
            print(f"\nFILES ({len(files)} total):")
            for file_info in files:
                comp_indicator = 'Z' if file_info['compressed'] else ' '
                cat = file_info['category'][:8].ljust(8)
                size = self._format_size(file_info['size'])
                print(f"{file_info['index']:3d} {comp_indicator} {cat} {size:>8} {file_info['name']}")
        retval = []
        def listfiles_json():
            # Detailed listing
            # print(f"\n📋 DETAILED FILE LISTING ({len(files)} files)")
            # print("-" * 80)
            # print("Idx Type     Category  Size      Uncomp    Name")
            # print("-" * 80)
            
            total_compressed = 0
            total_uncompressed = 0
            categories = {}

            
            nonlocal retval
            if not get:
                print("[")
            for file_info in files:
                comp_indicator = 'Z' if file_info['compressed'] else ' '
                cat = file_info['category'][:8].ljust(8)
                size = self._format_size(file_info['size'])
                uncomp_size = self._format_size(file_info['uncompressed_size'])
                
                # Track statistics
                total_compressed += file_info['size']
                total_uncompressed += file_info['uncompressed_size']
                categories[file_info['category']] = categories.get(file_info['category'], 0) + 1
                
                # print(f"\"filenum\":{file_info['index']:3d},\"compression\":\"{comp_indicator}\",\"{cat}\",\"{size:>8}\",\"{uncomp_size:>8}\",\"{file_info['name']}\"")

                o = {
                    "filenum": file_info['index'],
                    "compression": comp_indicator.strip(),
                    "category": cat.strip(),
                    "compressed_size": size,
                    "uncompressed_size": uncomp_size,
                    "filename": file_info['name']
                }
                if not get:
                    print(json.dumps(o))
                    if file_info is not files[-1]:
                      print(",")
                else:
                  retval.append(o)
            if not get:
                print("]")
            
            # print("-" * 80)
            # print(f"Total: {len(files)} files")
            # print(f"Compressed data: {self._format_size(total_compressed)}")
            # print(f"Uncompressed data: {self._format_size(total_uncompressed)}")
            # 
            # print(f"\n📊 CATEGORY BREAKDOWN:")
            # for category, count in sorted(categories.items()):
            #     print(f"  {category:10}: {count:3d} files")
        

        if get:
            show_json = True
        if show_json:
            listfiles_json()
        elif show_details:
            listfiles_showdetails()
        else:
            listfiles_nodetails()
        
        if get:
            return retval
        else:
            return files
    
    def _format_size(self, size_bytes):
        """Format size in human-readable format"""
        if size_bytes < 1024:
            return f"{size_bytes}B"
        elif size_bytes < 1024 * 1024:
            return f"{size_bytes/1024:.1f}K"
        elif size_bytes < 1024 * 1024 * 1024:
            return f"{size_bytes/(1024*1024):.1f}M"
        else:
            return f"{size_bytes/(1024*1024*1024):.1f}G"
    
    def extract_file_by_name(self, filename, output_path=None):
        """Extract a specific file by name"""
        files = self.extract_files()
        
        for file_info in files:
            if file_info['name'] == filename or file_info['unzipped_name'] == filename:
                if output_path:
                    output_file = Path(output_path)
                    output_file.parent.mkdir(parents=True, exist_ok=True)
                    
                    # Write uncompressed content if available, otherwise compressed
                    content = file_info['unzipped_content'] if file_info['unzipped_content'] else file_info['content']
                    
                    with open(output_file, 'wb') as f:
                        f.write(content)
                    
                    print(f"Extracted {filename} to {output_path} ({self._format_size(len(content))})")
                    return content
                else:
                    return file_info['unzipped_content'] if file_info['unzipped_content'] else file_info['content']
        
        raise FileNotFoundError(f"File not found: {filename}")
    
    def get_segment_info(self):
        """Get information about all ROM segments"""
        segments = {}
        
        for segment_name in self.SEGMENT_OFFSETS:
            try:
                offset = self._get_segment_offset(segment_name)
                if offset > 0:  # Skip empty segments
                    segments[segment_name] = {
                        'offset': offset,
                        'hex_offset': f"0x{offset:x}"
                    }
            except (ValueError, IndexError):
                continue
        
        return segments

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Perfect Dark ROM file extraction driver')
    parser.add_argument('rom', nargs='?', default='pd.ntsc-final.z64',
                       help='ROM file to analyze (default: pd.ntsc-final.z64)')
    parser.add_argument('-l', '--list', action='store_true',
                       help='List all files in ROM')
    parser.add_argument('-s', '--simple', action='store_true',
                       help='Simple file listing')
    parser.add_argument('-x', '--extract', metavar='FILENAME',
                       help='Extract specific file by name')
    parser.add_argument('-o', '--output', metavar='PATH',
                       help='Output path for extracted file')
    parser.add_argument('--segments', action='store_true',
                       help='Show segment information')
    
    args = parser.parse_args()
    
    try:
        driver = PDROMDriver(args.rom)
        
        if args.segments:
            segments = driver.get_segment_info()
            print(f"\n🗂️  ROM SEGMENTS ({driver.rom_variant}):")
            print("-" * 40)
            for name, info in segments.items():
                print(f"{name:15}: {info['hex_offset']:>10}")
        
        if args.list:
            driver.list_files(show_details=not args.simple)
        
        if args.extract:
            driver.extract_file_by_name(args.extract, args.output)
            
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
