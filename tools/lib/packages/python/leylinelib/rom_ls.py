#!/usr/bin/env python3
"""
ROM filesystem ls-like listing tool
Provides familiar directory-style output for ROM segments and files
"""

import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent))

from rompath import ROMPath
import struct

def format_size(size_bytes):
    """Format size in human-readable format"""
    if size_bytes < 1024:
        return f"{size_bytes}B"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes/1024:.1f}K"
    elif size_bytes < 1024 * 1024 * 1024:
        return f"{size_bytes/(1024*1024):.1f}M"
    else:
        return f"{size_bytes/(1024*1024*1024):.1f}G"

def get_segment_type_indicator(segment_name):
    """Get type indicator for ls-style output"""
    type_map = {
        'files': 'f',      # files
        'data': 'd',       # data
        'game': 'g',       # game code
        'animations': 'a', # animations
        'mpconfigs': 'm',  # multiplayer configs
        'textures': 't',   # textures
        'copyright': 'c'   # copyright
    }
    return type_map.get(segment_name, 's')  # 's' for segment

def analyze_segment_content(rom, segment_name):
    """Analyze segment content for additional info"""
    segment = rom / segment_name
    if not segment.exists():
        return "empty", 0
    
    try:
        data = segment.read_bytes()
        if len(data) == 0:
            return "empty", 0
        
        # Basic content analysis
        if segment_name == 'textures':
            # Look for texture headers
            texture_count = 0
            offset = 0
            while offset < len(data) - 8:
                try:
                    # Simple heuristic for texture data
                    if data[offset:offset+2] in [b'\x46\x0a', b'\x00\x00']:
                        texture_count += 1
                    offset += 1024  # Skip ahead
                    if texture_count > 50:  # Reasonable limit for search
                        break
                except:
                    break
            return "textures", texture_count if texture_count > 0 else "data"
        
        elif segment_name == 'files':
            # File table analysis
            file_count = 0
            offset = 0
            while offset < min(len(data), 8192):  # Check first 8KB
                try:
                    if offset + 4 <= len(data):
                        word = struct.unpack('>I', data[offset:offset+4])[0]
                        if 0x1000 <= word <= 0x2000000:  # Reasonable file size/offset range
                            file_count += 1
                    offset += 4
                except:
                    break
            return "filetable", file_count // 2 if file_count > 10 else "data"
        
        elif segment_name == 'game':
            return "code", "executable"
        
        elif segment_name == 'animations':
            return "anims", "compressed"
            
        elif segment_name == 'mpconfigs':
            return "configs", "multiplayer"
            
        elif segment_name == 'data':
            return "data", "binary"
            
        elif segment_name == 'copyright':
            return "legal", "text"
        
        else:
            return "data", "binary"
            
    except Exception as e:
        return "error", str(e)

def rom_ls(rom_path="pd.ntsc-final.z64", long_format=False, human_readable=True):
    """List ROM contents in ls-style format"""
    
    try:
        rom = ROMPath(rom_path)
        
        if not long_format:
            # Simple listing
            print("ROM segments:")
            segments = rom.segments()
            for segment in segments:
                seg = rom / segment
                size = format_size(seg.stat().st_size) if human_readable else str(seg.stat().st_size)
                type_char = get_segment_type_indicator(segment)
                print(f"{type_char}  {segment:<12} {size:>8}")
        else:
            # Long format listing (like ls -l)
            print("ROM filesystem listing:")
            print("Type  Segment      Size     Content      Description")
            print("-" * 60)
            
            segments = rom.segments()
            total_size = 0
            
            for segment in segments:
                seg = rom / segment
                stat = seg.stat()
                size = stat.st_size
                total_size += size
                
                type_char = get_segment_type_indicator(segment)
                size_str = format_size(size) if human_readable else str(size)
                
                content_type, content_desc = analyze_segment_content(rom, segment)
                
                print(f"{type_char}     {segment:<12} {size_str:>8} {content_type:<12} {content_desc}")
            
            print("-" * 60)
            print(f"Total: {len(segments)} segments, {format_size(total_size) if human_readable else total_size}")
            
            # ROM info
            print(f"\nROM: {rom_path}")
            print(f"Type: {rom.detect_rom_type()}")
            print(f"Patched: {'Yes' if rom.patch_file else 'No'}")
            
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description='List ROM filesystem contents')
    parser.add_argument('rom', nargs='?', default='pd.ntsc-final.z64',
                       help='ROM file to list (default: pd.ntsc-final.z64)')
    parser.add_argument('-l', '--long', action='store_true',
                       help='Use long listing format')
    parser.add_argument('-b', '--bytes', action='store_true',
                       help='Show sizes in bytes instead of human-readable')
    
    args = parser.parse_args()
    
    return rom_ls(args.rom, long_format=args.long, human_readable=not args.bytes)

if __name__ == '__main__':
    sys.exit(main())