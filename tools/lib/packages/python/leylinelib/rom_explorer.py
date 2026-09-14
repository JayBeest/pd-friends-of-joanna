#!/usr/bin/env python3
"""
Perfect Dark ROM Explorer - Interactive pathlib-like interface

This tool demonstrates the ROM Path system's pathlib.Path-like interface
for exploring Perfect Dark ROMs interactively.
"""

import sys
from pathlib import Path

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from rompath import ROMPath


def format_size(size: int) -> str:
    """Format size in human readable format"""
    if size < 1024:
        return f"{size} B"
    elif size < 1024 * 1024:
        return f"{size / 1024:.1f} KB"
    elif size < 1024 * 1024 * 1024:
        return f"{size / 1024 / 1024:.1f} MB"
    else:
        return f"{size / 1024 / 1024 / 1024:.1f} GB"


def show_rom_overview(rom: ROMPath):
    """Show ROM overview"""
    print("🎮 Perfect Dark ROM Overview")
    print("=" * 35)
    
    # Basic ROM info
    stat = rom.stat()
    print(f"ROM Size: {format_size(stat.st_size)}")
    print(f"ROM Type: {stat.rom_type}")
    print(f"Patched: {'Yes' if stat.patched else 'No'}")
    
    # List segments
    segments = rom.segments()
    print(f"Segments: {len(segments)}")
    
    for segment_name in segments:
        segment = rom / segment_name
        if segment.exists():
            seg_stat = segment.stat()
            print(f"  📁 {segment_name:<12} {format_size(seg_stat.st_size)}")


def demonstrate_pathlib_interface(rom: ROMPath):
    """Demonstrate pathlib-like interface"""
    print("\n🛠️ pathlib.Path-like Interface Demo")
    print("=" * 40)
    
    examples = [
        # Basic navigation
        ("rom.segments()", "List available segments"),
        ("textures = rom / 'textures'", "Navigate to textures segment"),
        ("textures.exists()", "Check if segment exists"),
        ("textures.stat().st_size", "Get segment size"),
        
        # File operations
        ("data = textures.read_bytes()[:32]", "Read first 32 bytes"),
        ("with (rom / 'game').open('rb') as f: header = f.read(16)", "File-like interface"),
        ("(rom / 'animations').extract_to('animations.bin')", "Extract to filesystem"),
    ]
    
    print("Available operations:")
    for code, description in examples:
        print(f"\n💡 {description}")
        print(f"   Code: {code}")
        
        try:
            if "segments()" in code:
                result = rom.segments()
                print(f"   → {result}")
                
            elif "textures = rom" in code:
                textures = rom / 'textures'
                print(f"   → Created ROMPathSegment for textures")
                
            elif "exists()" in code:
                textures = rom / 'textures'
                result = textures.exists()
                print(f"   → {result}")
                
            elif "stat().st_size" in code:
                textures = rom / 'textures'
                result = textures.stat().st_size
                print(f"   → {format_size(result)}")
                
            elif "read_bytes()" in code:
                textures = rom / 'textures'
                data = textures.read_bytes()[:32]
                print(f"   → {data.hex()}")
                
            elif "open('rb')" in code:
                with (rom / 'game').open('rb') as f:
                    header = f.read(16)
                print(f"   → {header.hex()}")
                
            elif "extract_to" in code:
                # Don't actually extract, just show it would work
                print(f"   → Would extract animations segment to file")
                
        except Exception as e:
            print(f"   ⚠️ Error: {e}")


def explore_segments_interactively(rom: ROMPath):
    """Interactive segment exploration"""
    print("\n🔍 Interactive Segment Explorer")
    print("=" * 35)
    
    segments = rom.segments()
    
    for segment_name in segments:
        print(f"\n📦 {segment_name.upper()} SEGMENT")
        print("-" * 25)
        
        try:
            segment = rom / segment_name
            
            if not segment.exists():
                print("❌ Segment not accessible")
                continue
            
            # Get segment info
            stat = segment.stat()
            print(f"Size: {format_size(stat.st_size)}")
            
            # Read and analyze first chunk
            data = segment.read_bytes()
            
            if len(data) > 0:
                # Show hex dump of first 64 bytes
                print("First 64 bytes:")
                for i in range(0, min(64, len(data)), 16):
                    hex_part = " ".join(f"{data[i+j]:02x}" for j in range(min(16, len(data)-i)))
                    ascii_part = "".join(chr(data[i+j]) if 32 <= data[i+j] <= 126 else '.' 
                                       for j in range(min(16, len(data)-i)))
                    print(f"  {i:04x}: {hex_part:<47} |{ascii_part}|")
                
                # Analyze content
                if data[:2] == b'\x11\x73':
                    print("🗜️ Contains Perfect Dark compressed data")
                elif data[:4] == b'MIPS':
                    print("💻 Contains MIPS executable code")
                elif segment_name == 'textures' and len(data) > 100000:
                    print("🎨 Contains texture data and table")
                elif segment_name == 'files':
                    print("📋 Contains file table and metadata")
                else:
                    # Check for patterns
                    null_count = data[:1000].count(0)
                    if null_count > 800:
                        print("📄 Mostly null bytes (sparse data)")
                    else:
                        print("📊 Binary data")
            
            # Show navigation path
            print(f"Access via: rom / '{segment_name}'")
            
        except Exception as e:
            print(f"❌ Error exploring {segment_name}: {e}")


def show_texture_exploration(rom: ROMPath):
    """Show texture-specific exploration"""
    print("\n🎨 Texture Segment Deep Dive")
    print("=" * 35)
    
    try:
        textures = rom / "textures"
        
        if not textures.exists():
            print("❌ Textures segment not found")
            return
        
        data = textures.read_bytes()
        print(f"Texture segment size: {format_size(len(data))}")
        
        # Show texture segment structure
        print("\n📋 Texture Segment Structure:")
        print("  0x000000 - 0x291D5F: Raw texture data")
        print("  0x291D60 - EOF:      Texture table")
        
        # Analyze texture table area
        table_start = 0x291d60
        if len(data) > table_start:
            table_data = data[table_start:table_start + 100]  # First 100 bytes of table
            
            print(f"\n🔍 Texture table preview (at 0x{table_start:X}):")
            for i in range(0, min(80, len(table_data)), 8):
                if i + 8 <= len(table_data):
                    entry = table_data[i:i+8]
                    if entry != b'\x00' * 8:
                        # Parse as texture entry
                        start = (entry[0] << 16) | (entry[1] << 8) | entry[2]
                        end = (entry[3] << 16) | (entry[4] << 8) | entry[5]
                        size = end - start
                        
                        if 0 < size < len(data):
                            print(f"  Entry {i//8:2d}: 0x{start:06X}-0x{end:06X} ({size:5d} bytes)")
        
        # Show how to access texture data
        print(f"\n💡 Usage Examples:")
        print(f"  textures = rom / 'textures'")
        print(f"  texture_data = textures.read_bytes()")
        print(f"  textures.extract_to('all_textures.bin')")
        
    except Exception as e:
        print(f"❌ Error exploring textures: {e}")


def main():
    """Main explorer function"""
    print("🦝 Perfect Dark ROM Explorer")
    print("pathlib.Path-like Interface Demo")
    print("=" * 50)
    
    # Find ROM file
    possible_roms = [
        "pd.ntsc-final.z64",
        "../pd.ntsc-final.z64", 
        "/home/catherine/src/pd/perfect-dark/pd.ntsc-final.z64"
    ]
    
    rom_file = None
    for rom_path in possible_roms:
        if Path(rom_path).exists():
            rom_file = Path(rom_path)
            break
    
    if not rom_file:
        print("❌ Perfect Dark ROM not found!")
        print("   Place pd.ntsc-final.z64 in current directory or update script path")
        return
    
    try:
        # Create ROM Path instance
        rom = ROMPath(rom_file)
        print(f"📀 Loaded: {rom_file.name}")
        print(f"🎯 Detected: {rom.detect_rom_type()}")
        
        # Run all demonstrations
        show_rom_overview(rom)
        demonstrate_pathlib_interface(rom)
        explore_segments_interactively(rom)
        show_texture_exploration(rom)
        
        print("\n🎉 ROM Exploration Complete!")
        print("\nThe ROM Path system provides a clean, pathlib-like interface")
        print("for exploring ROM contents programmatically. Perfect for:")
        print("  • ROM analysis and reverse engineering")
        print("  • Asset extraction and conversion tools") 
        print("  • Game modding and content creation")
        print("  • Educational exploration of ROM structure")
        
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()