#!/usr/bin/env python3
"""
Perfect Dark ROM Toolkit - Unified Interface
============================================

A comprehensive tool for analyzing and extracting Perfect Dark ROM files.
Combines functionality from:
- pd_rom_driver.py (file extraction)
- rom_ls.py (directory listing) 
- rom_explorer.py (interactive exploration)
- rompath.py (pathlib interface)

Attribution
-----------
The ROM extraction/listing/exploration backends bundled here are
derived from Ryan Dwyer's pd-extract scripts (perfect-dark decomp
tooling). The unified CLI wrapper is our own.

Usage:
    # List ROM segments (ls-style)
    python pdrom.py list
    python pdrom.py list --long
    
    # List all files in ROM
    python pdrom.py files
    python pdrom.py files --simple
    
    # Extract files
    python pdrom.py extract --file "bgdata/bg_dam.seg"
    python pdrom.py extract --all --output extracted/
    
    # Interactive exploration
    python pdrom.py explore
    
    # Show ROM information
    python pdrom.py info
    python pdrom.py info --segments
    
    # Library usage
    from pdrom import PDROMToolkit
    rom = PDROMToolkit("pd.ntsc-final.z64")
    files = rom.list_files()
"""

import sys
import argparse
from pathlib import Path
from typing import List, Dict, Optional, Union

# Add current directory for imports
sys.path.insert(0, str(Path(__file__).parent))

# Import our existing tools
from rompath import ROMPath
from pd_rom_driver import PDROMDriver
import rom_ls
import rom_explorer
import os
import struct
import zlib
import json


class PDROMToolkit:
    """
    Unified Perfect Dark ROM analysis toolkit
    
    Provides a single interface for all ROM operations including:
    - File listing and extraction
    - Segment analysis
    - Interactive exploration
    - pathlib-like ROM navigation
    """
    
    def __init__(self, rom_path: str = "pd.ntsc-final.z64"):
        """Initialize ROM toolkit with specified ROM file"""
        self.rom_path = Path(rom_path)
        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM file not found: {rom_path}")
            
        # Initialize component systems
        self._rompath = ROMPath(str(rom_path))
        self._driver = PDROMDriver(str(rom_path))
        
    @property
    def rompath(self) -> ROMPath:
        """Access to pathlib-like ROM interface"""
        return self._rompath
        
    @property
    def driver(self) -> PDROMDriver:
        """Access to ROM file extraction driver"""
        return self._driver
    
    # === LISTING OPERATIONS ===
    
    def list_segments(self, long_format: bool = False, bytes_format: bool = False) -> List[Dict]:
        """List ROM segments with ls-style output"""
        segments = []
        
        for segment_name in self._rompath.segments():
            segment = self._rompath / segment_name
            size = segment.stat().st_size
            
            segment_info = {
                'name': segment_name,
                'size': size,
                'type_indicator': rom_ls.get_segment_type_indicator(segment_name),
                'formatted_size': self._format_size(size, bytes_format)
            }
            segments.append(segment_info)
            
            if not long_format:
                # Simple format
                print(f"{segment_info['type_indicator']}  {segment_name:12} {segment_info['formatted_size']:>8}")
            else:
                # Long format with additional info
                content_info = rom_ls.analyze_segment_content(self._rompath, segment_name)
                print(f"{segment_info['type_indicator']}rw-rw-rw- 1 rom rom {segment_info['formatted_size']:>8} Oct 14  2025 {segment_name}")
                if content_info:
                    print(f"    {content_info}")
                    
        return segments
    
    def list_files(self, simple: bool = False, json: bool = False) -> List[Dict]:
        """List all files in ROM using file extraction driver"""
        return self._driver.list_files(show_details=(not simple) and (not json), show_json=json)
    
    def count_files(self) -> Dict[str, int]:
        """Get file count statistics"""
        files = self._driver.get_files()  # Use silent method
        total = len(files)
        
        categories = {}
        for file_info in files:
            category = file_info.get('category', 'unknown')
            categories[category] = categories.get(category, 0) + 1
            
        return {
            'total': total,
            'by_category': categories
        }
    
    # === EXTRACTION OPERATIONS ===
    
    def extract_file(self, filename: str, output_path: Optional[str] = None) -> bool:
        """Extract a specific file by name"""
        return self._driver.extract_file_by_name(filename, output_path)
    
    def extract_all_files(self, output_dir: str = "extracted") -> int:
        """Extract all files to specified directory"""
        files = self.list_files(simple=True)
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)
        
        extracted_count = 0
        for file_info in files:
            filename = file_info['filename']
            if self.extract_file(filename, str(output_path / filename)):
                extracted_count += 1
                
        return extracted_count
    
    def extract_segment(self, segment_name: str, output_path: str) -> bool:
        """Extract entire segment to file"""
        try:
            segment = self._rompath / segment_name
            if not segment.exists():
                return False
                
            with open(output_path, 'wb') as f:
                f.write(segment.read_bytes())
            return True
        except Exception:
            return False
    
    # === INFORMATION OPERATIONS ===
    
    def get_rom_info(self) -> Dict:
        """Get comprehensive ROM information"""
        return {
            'path': str(self.rom_path),
            'size': self.rom_path.stat().st_size,
            'type': self._rompath.detect_rom_type(),
            'variant': self._driver.rom_variant,
            'segments': list(self._rompath.segments()),
            'file_count': self.count_files()['total'],
            'patched': getattr(self._rompath, 'patched', False)
        }
    
    def get_segment_info(self) -> Dict[str, Dict]:
        """Get detailed segment information"""
        segment_info = {}
        
        for segment_name in self._rompath.segments():
            segment = self._rompath / segment_name
            segment_info[segment_name] = {
                'size': segment.stat().st_size,
                'formatted_size': self._format_size(segment.stat().st_size),
                'type_indicator': rom_ls.get_segment_type_indicator(segment_name),
                'content_info': rom_ls.analyze_segment_content(self._rompath, segment_name)
            }
            
        return segment_info
    
    # === INTERACTIVE OPERATIONS ===
    
    def explore_interactive(self):
        """Launch interactive ROM exploration"""
        rom_explorer.show_rom_overview(self._rompath)
        print()
        rom_explorer.demonstrate_pathlib_interface(self._rompath)
        print()
        rom_explorer.explore_segments_interactively(self._rompath)
    
    def show_overview(self):
        """Show ROM overview"""
        rom_explorer.show_rom_overview(self._rompath)
    
    def demonstrate_pathlib(self):
        """Demonstrate pathlib-like interface"""
        rom_explorer.demonstrate_pathlib_interface(self._rompath)
    
    # === UTILITY METHODS ===
    
    def _format_size(self, size_bytes: int, bytes_format: bool = False) -> str:
        """Format size in human-readable format"""
        if bytes_format:
            return str(size_bytes)
            
        if size_bytes < 1024:
            return f"{size_bytes}B"
        elif size_bytes < 1024 * 1024:
            return f"{size_bytes/1024:.1f}K"
        elif size_bytes < 1024 * 1024 * 1024:
            return f"{size_bytes/(1024*1024):.1f}M"
        else:
            return f"{size_bytes/(1024*1024*1024):.1f}G"
    
    # === SEARCH OPERATIONS ===
    
    def search_files(self, pattern: str) -> List[Dict]:
        """Search for files matching pattern"""
        files = self._driver.get_files()  # Use silent method
        matching_files = []
        
        for file_info in files:
            filename = file_info.get('name', '')  # Use 'name' instead of 'filename'
            if pattern.lower() in filename.lower():
                matching_files.append(file_info)
                
        return matching_files
    
    def find_files_by_category(self, category: str) -> List[Dict]:
        """Find files by category (A=audio, C=chrs, G=guns, etc.)"""
        files = self._driver.get_files()  # Use silent method
        matching_files = []
        
        for file_info in files:
            if file_info.get('category', '').lower() == category.lower():
                matching_files.append(file_info)
                
        return matching_files


def cmd_filetable(args) -> int:
    """Build file table binary from JSON"""
    if not args.input or not args.output:
        print("Error: Input and output files are required for build")
        return 1

    try:
        with open(args.input, 'r') as f:
            data = json.load(f)

        with open(args.output, 'wb') as f:
            f.write(b'PDFT')
            f.write(struct.pack('>I', 1))
            f.write(struct.pack('>I', len(data)))

            for entry in data:
                file_id = entry.get('id', 0)
                offset = entry.get('offset', 0)
                size = entry.get('size', 0)
                name = entry.get('name', "")
                path = entry.get('path', "")
                
                flags = 0
                if 'offset' in entry:
                    flags |= 1
                if 'path' in entry:
                    flags |= 2

                f.write(struct.pack('>IIII', file_id, flags, offset, size))
                
                name_bytes = name.encode('utf-8') + b'\0'
                f.write(struct.pack('>H', len(name_bytes)))
                f.write(name_bytes)

                path_bytes = path.encode('utf-8') + b'\0'
                f.write(struct.pack('>H', len(path_bytes)))
                f.write(path_bytes)
        
        print(f"✓ Built {args.output} from {args.input}")
        return 0
    except Exception as e:
        print(f"Error building file table: {e}")
        return 1


def create_parser() -> argparse.ArgumentParser:
    """Create command line argument parser"""
    parser = argparse.ArgumentParser(
        description='Perfect Dark ROM Toolkit - Unified ROM analysis and extraction',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s list                     # List ROM segments
  %(prog)s list --long              # Detailed segment listing
  %(prog)s list --json             # JSON segment listing
  %(prog)s files                    # List all files in ROM
  %(prog)s files --simple           # Simple file listing
  %(prog)s files --json            # JSON file listing
  %(prog)s extract --file "bg_dam.seg"  # Extract specific file
  %(prog)s extract --all --output extracted/  # Extract all files
  %(prog)s info                     # Show ROM information
  %(prog)s explore                  # Interactive exploration
  %(prog)s search --pattern "gun"   # Search for files containing "gun"
        """
    )
    
    default_rom = os.environ.get('PD_ROMFILE', 'pd.ntsc-final.z64')
    parser.add_argument('rom', nargs='?', default=default_rom,
                       help=f'ROM file to analyze (default: {default_rom})')
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # List command
    list_parser = subparsers.add_parser('list', help='List ROM segments')
    list_parser.add_argument('-l', '--long', action='store_true',
                            help='Use long listing format')
    list_parser.add_argument('-b', '--bytes', action='store_true',
                            help='Show sizes in bytes')
    list_parser.add_argument('--json', action='store_true',
                            help='Output JSON instead of text')
    
    # Files command
    files_parser = subparsers.add_parser('files', help='List all files in ROM')
    files_parser.add_argument('-s', '--simple', action='store_true',
                             help='Simple file listing')
    files_parser.add_argument('--json', action='store_true',
                             help='Output JSON instead of text')
    
    # Extract command
    extract_parser = subparsers.add_parser('extract', help='Extract files or segments')
    extract_parser.add_argument('-f', '--file', help='Extract specific file by name')
    extract_parser.add_argument('-s', '--segment', help='Extract specific segment')
    extract_parser.add_argument('-a', '--all', action='store_true',
                               help='Extract all files')
    extract_parser.add_argument('-o', '--output', default='.',
                               help='Output path/directory (default: current directory)')
    extract_parser.add_argument('--json', action='store_true',
                               help='Output JSON instead of text')
    
    # Info command
    info_parser = subparsers.add_parser('info', help='Show ROM information')
    info_parser.add_argument('--segments', action='store_true',
                            help='Show detailed segment information')
    info_parser.add_argument('--json', action='store_true',
                            help='Output JSON instead of text')
    
    # Explore command
    explore_parser = subparsers.add_parser('explore', help='Interactive ROM exploration')
    explore_parser.add_argument('--overview', action='store_true',
                               help='Show overview only')
    explore_parser.add_argument('--pathlib', action='store_true',
                               help='Demonstrate pathlib interface only')
    explore_parser.add_argument('--json', action='store_true',
                               help='Output JSON instead of text')
    
    # Search command
    search_parser = subparsers.add_parser('search', help='Search for files')
    search_parser.add_argument('-p', '--pattern', required=True,
                              help='Search pattern')
    search_parser.add_argument('-c', '--category',
                              help='Search by category (audio, chrs, guns, props, setup, bgdata, lang, objects, unknown)')
    search_parser.add_argument('--json', action='store_true',
                              help='Output JSON instead of text')
    
    # Count command
    count_parser = subparsers.add_parser('count', help='Show file count statistics')
    count_parser.add_argument('--json', action='store_true',
                             help='Output JSON instead of text')
    
    # Filetable command
    filetable_parser = subparsers.add_parser('filetable', help='Manage external file table')
    filetable_parser.add_argument('action', choices=['build'], help='Action to perform')
    filetable_parser.add_argument('-i', '--input', help='Input JSON file')
    filetable_parser.add_argument('-o', '--output', help='Output binary file')

    return parser


def main():
    """Main CLI interface"""
    parser = create_parser()
    args = parser.parse_args()
    
    # Handle no command case
    if not args.command:
        parser.print_help()
        return

    def pdromcmd_list():
        if json_flag:
            result = toolkit.list_segments(long_format=args.long, bytes_format=args.bytes)
            print(json.dumps(result, indent=2))
        else:
            print("ROM segments:")
            toolkit.list_segments(long_format=args.long, bytes_format=args.bytes)

    def pdromcmd_files():
        toolkit.list_files(simple=args.simple, json=args.json)

    def pdromcmd_extract():
        if json_flag:
            result = {"status": "success", "files_extracted": 0}
            extracted_items = []
            
            if args.file:
                output_path = args.output
                if output_path and Path(output_path).is_dir():
                    output_path = str(Path(output_path) / args.file)
                
                success = toolkit.extract_file(args.file, output_path)
                if success:
                    result["status"] = "success"
                    extracted_items.append({"filename": args.file, "output_path": output_path})
                else:
                    result["status"] = "error"
                    result["error"] = f"Failed to extract: {args.file}"
            
            elif args.segment:
                output_path = args.output if args.output != '.' else f"{args.segment}.bin"
                success = toolkit.extract_segment(args.segment, output_path)
                if success:
                    result["status"] = "success"
                    extracted_items.append({"segment": args.segment, "output_path": output_path})
                else:
                    result["status"] = "error"
                    result["error"] = f"Failed to extract segment: {args.segment}"
            
            elif args.all:
                output_dir = args.output if args.output != '.' else 'extracted'
                count = toolkit.extract_all_files(output_dir)
                result["status"] = "success"
                result["output_dir"] = output_dir
                result["files_extracted"] = count
                result["extracted_items"] = [{"type": "all_files", "count": count, "output_dir": output_dir}]
            
            print(json.dumps(result, indent=2))
        else:
            if args.file:
                output_path = args.output
                if output_path and Path(output_path).is_dir():
                    output_path = str(Path(output_path) / args.file)
                
                if toolkit.extract_file(args.file, output_path):
                    print(f"✓ Extracted: {args.file}")
                else:
                    print(f"✗ Failed to extract: {args.file}")
                    return 1
            elif args.segment:
                output_path = args.output if args.output != '.' else f"{args.segment}.bin"
                if toolkit.extract_segment(args.segment, output_path):
                    print(f"✓ Extracted segment: {args.segment} → {output_path}")
                else:
                    print(f"✗ Failed to extract segment: {args.segment}")
                    return 1
            elif args.all:
                output_dir = args.output if args.output != '.' else 'extracted'
                count = toolkit.extract_all_files(output_dir)
                print(f"✓ Extracted {count} files to {output_dir}/")
            else:
                print("Error: Specify --file, --segment, or --all for extraction")
                return 1

    def pdromcmd_info():
        info = toolkit.get_rom_info()
        if json_flag:
            print(json.dumps(info, indent=2))
        else:
            print(f"🎮 ROM Information")
            print(f"Path: {info['path']}")
            print(f"Size: {toolkit._format_size(info['size'])}")
            print(f"Type: {info['type']}")
            print(f"Variant: {info['variant']}")
            print(f"Segments: {len(info['segments'])}")
            print(f"Files: {info['file_count']}")
            
            if args.segments:
                print(f"\n📂 Segment Details:")
                segment_info = toolkit.get_segment_info()
                for name, details in segment_info.items():
                    print(f"  {name}: {details['formatted_size']}")
                    if details['content_info']:
                        print(f"    {details['content_info']}")

    def pdromcmd_explore():
        if args.json:
            result = {
                "rom_path": str(toolkit._rompath),
                "segments": list(toolkit._rompath.segments()),
                "pathlib_interface": "Available via rompath property",
                "driver": "Available via driver property",
                "file_count": toolkit.count_files()['total']
            }
            print(json.dumps(result, indent=2))
        else:
            if args.overview:
                toolkit.show_overview()
            elif args.pathlib:
                toolkit.demonstrate_pathlib()
            else:
                toolkit.explore_interactive()

    def pdromcmd_search():
        if args.json:
            if args.category:
                matches = toolkit.find_files_by_category(args.category)
                result = {
                    "category": args.category,
                    "count": len(matches),
                    "matches": matches
                }
            else:
                matches = toolkit.search_files(args.pattern)
                result = {
                    "pattern": args.pattern,
                    "count": len(matches),
                    "matches": matches
                }
            print(json.dumps(result, indent=2))
        else:
            if args.category:
                matches = toolkit.find_files_by_category(args.category)
                print(f"Files in category '{args.category}': {len(matches)}")
            else:
                matches = toolkit.search_files(args.pattern)
                print(f"Files matching '{args.pattern}': {len(matches)}")
                
            for match in matches[:20]:
                filename = match.get('name', 'Unknown')
                size = match.get('size', 0)
                print(f"  {filename} ({toolkit._format_size(size)})")
                
            if len(matches) > 20:
                print(f"  ... and {len(matches) - 20} more")
    def pdromcmd_count():
        counts = toolkit.count_files()
        if json_flag:
            print(json.dumps(counts, indent=2))
        else:
            print(f"📊 File Statistics")
            print(f"Total files: {counts['total']}")
            print(f"By category:")
            for category, count in sorted(counts['by_category'].items()):
                print(f"  {category}: {count}")
    def pdromcmd_filetable():
        if args.action == 'build':
            return cmd_filetable(args)
    # ==================== JSON OUTPUT CONTROLLER ====================
    json_flag = getattr(args, 'json', False)
    
    # Initialize toolkit
    try:
        toolkit = PDROMToolkit(args.rom)
    except FileNotFoundError as e:
        if json_flag:
            print(json.dumps({"error": str(e), "type": "file_not_found"}))
        else:
            print(f"Error: {e}")
        return 1
    except Exception as e:
        if json_flag:
            print(json.dumps({"error": str(e), "type": "load_error"}))
        else:
            print(f"Error loading ROM: {e}")
        return 1
    
    def dispatch_pdromcmd(key):
        {
            "files": pdromcmd_files,
            "extract": pdromcmd_extract,
            "list": pdromcmd_list,
            "filetable": pdromcmd_filetable,
            "search": pdromcmd_search,
            "count": pdromcmd_count,
            "info": pdromcmd_info,
            "explore": pdromcmd_explore
        }[key]()
    try:
        dispatch_pdromcmd(args.command)
    except KeyboardInterrupt:
        if json_flag:
            print(json.dumps({"error": "interrupted", "type": "interrupted"}))
        else:
            print("\nInterrupted by user")
        return 1
    except Exception as e:
        if json_flag:
            print(json.dumps({"error": str(e), "type": "runtime_error"}))
            print(e, file=sys.stderr)
            raise
        else:
            print(f"Error: {e}")
        return 1
        
    return 0


if __name__ == '__main__':
    exit(main())
