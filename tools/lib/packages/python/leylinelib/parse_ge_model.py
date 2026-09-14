#!/usr/bin/env python3
"""
Parse GoldenEye ModelFileHeader binary files
"""

import struct
import sys
from pathlib import Path

class GEModelFileHeader:
    """GoldenEye ModelFileHeader structure"""
    def __init__(self, data):
        self.data = data
        self.parse()
    
    def parse(self):
        """Parse the header from binary data"""
        # ModelFileHeader structure (32 bytes without debug, 36 with debug)
        if len(self.data) < 32:
            raise ValueError(f"Data too short: {len(self.data)} bytes")
        
        # Unpack the header
        # typedef struct ModelFileHeader {
        #     ModelNode *RootNode;            // 0x00: 4 bytes
        #     ModelSkeleton *Skeleton;        // 0x04: 4 bytes
        #     ModelNode **Switches;           // 0x08: 4 bytes
        #     s16 numSwitches;                // 0x0C: 2 bytes
        #     s16 numMatrices;                // 0x0E: 2 bytes
        #     f32 BoundingVolumeRadius;       // 0x10: 4 bytes
        #     s16 numRecords;                 // 0x14: 2 bytes
        #     s16 numtextures;                // 0x16: 2 bytes
        #     ModelFileTextures *Textures;    // 0x18: 4 bytes
        #     s32 isLoaded;                   // 0x1C: 4 bytes (not in EU version)
        # } ModelFileHeader;
        
        # Big-endian format for N64
        fmt = '>IIIHHFHHII'  # Big-endian
        values = struct.unpack(fmt, self.data[0:32])
        
        self.RootNode = values[0]
        self.Skeleton = values[1]
        self.Switches = values[2]
        self.numSwitches = values[3]
        self.numMatrices = values[4]
        self.BoundingVolumeRadius = values[5]
        self.numRecords = values[6]
        self.numtextures = values[7]
        self.Textures = values[8]
        self.isLoaded = values[9]
    
    def __str__(self):
        """String representation"""
        lines = [
            "GoldenEye ModelFileHeader:",
            f"  RootNode:            0x{self.RootNode:08X}",
            f"  Skeleton:            0x{self.Skeleton:08X}",
            f"  Switches:            0x{self.Switches:08X}",
            f"  numSwitches:         {self.numSwitches}",
            f"  numMatrices:         {self.numMatrices}",
            f"  BoundingVolumeRadius: {self.BoundingVolumeRadius:.2f}",
            f"  numRecords:          {self.numRecords}",
            f"  numtextures:         {self.numtextures}",
            f"  Textures:            0x{self.Textures:08X}",
            f"  isLoaded:            0x{self.isLoaded:08X}",
        ]
        return '\n'.join(lines)


class GEModelFileTextures:
    """GoldenEye ModelFileTextures structure (8 bytes each)"""
    SIZE = 8
    
    def __init__(self, data):
        self.parse(data)
    
    def parse(self, data):
        """Parse texture entry"""
        if len(data) < self.SIZE:
            raise ValueError(f"Texture data too short: {len(data)} bytes")
        
        # typedef struct ModelFileTextures {
        #     u32 TextureID;       // 0x00: 4 bytes
        #     u8  Width;           // 0x04: 1 byte
        #     u8  Height;          // 0x05: 1 byte
        #     u8  MipMapTiles;     // 0x06: 1 byte
        #     u8  Type;            // 0x07: 1 byte
        #     u8  RenderDepth;     // 0x08: 1 byte (CI uses 16bit)
        #     u8  sflags;          // 0x09: 1 byte (G_TX_)
        #     u8  tflags;          // 0x0A: 1 byte (G_TX_)
        # } ModelFileTextures;    // 0x0B: 1 byte padding to 8 bytes?
        
        # Big-endian format
        fmt = '>I4B'
        values = struct.unpack(fmt, data[0:self.SIZE])
        
        self.TextureID = values[0]
        self.Width = values[1]
        self.Height = values[2]
        self.MipMapTiles = values[3]
        self.Type = values[4]
        
        # Remaining bytes
        if len(data) >= 8:
            self.RenderDepth = data[5]
            self.sflags = data[6]
            self.tflags = data[7]
        else:
            self.RenderDepth = 0
            self.sflags = 0
            self.tflags = 0
    
    def __str__(self):
        """String representation"""
        return (f"Texture: ID=0x{self.TextureID:08X}, "
                f"{self.Width}x{self.Height}, "
                f"Mips={self.MipMapTiles}, Type={self.Type}, "
                f"Depth={self.RenderDepth}, s={self.sflags}, t={self.tflags}")


def parse_model_file(filepath):
    """Parse a GE model file and display its structure"""
    path = Path(filepath)
    if not path.exists():
        print(f"Error: File not found: {filepath}")
        return
    
    data = path.read_bytes()
    print(f"\nFile: {path.name}")
    print(f"Size: {len(data):,} bytes ({len(data)} bytes)")
    print("=" * 70)
    
    # Parse header
    try:
        header = GEModelFileHeader(data)
        print(header)
        
        # Try to parse textures if we have enough data
        if header.numtextures > 0:
            print(f"\nTextures ({header.numtextures}):")
            print("-" * 70)
            
            # Textures typically follow the header
            # But we need to know where they are in the file
            # For now, let's try a few common offsets
            
            # Try parsing textures starting right after header
            tex_offset = 32
            if tex_offset + (header.numtextures * GEModelFileTextures.SIZE) <= len(data):
                for i in range(min(header.numtextures, 10)):  # Limit to first 10
                    tex_data = data[tex_offset + (i * GEModelFileTextures.SIZE):]
                    try:
                        tex = GEModelFileTextures(tex_data)
                        print(f"  [{i}] {tex}")
                    except Exception as e:
                        print(f"  [{i}] Error parsing: {e}")
            else:
                print(f"  Not enough data at offset 0x{tex_offset:X}")
                print(f"  (Need {header.numtextures * GEModelFileTextures.SIZE} bytes, have {len(data) - tex_offset})")
        
        # Show first 128 bytes as hex for inspection
        print(f"\nFirst 128 bytes (hex):")
        print("-" * 70)
        for i in range(0, min(128, len(data)), 16):
            hex_str = ' '.join(f'{b:02X}' for b in data[i:i+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            print(f"{i:04X}:  {hex_str:<48}  {ascii_str}")
        
    except Exception as e:
        print(f"\nError parsing header: {e}")
        print("\nShowing first 128 bytes anyway:")
        for i in range(0, min(128, len(data)), 16):
            hex_str = ' '.join(f'{b:02X}' for b in data[i:i+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            print(f"{i:04X}:  {hex_str:<48}  {ascii_str}")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: parse_ge_model.py <model_file.bin> [...]")
        sys.exit(1)
    
    for filepath in sys.argv[1:]:
        parse_model_file(filepath)
        print()
