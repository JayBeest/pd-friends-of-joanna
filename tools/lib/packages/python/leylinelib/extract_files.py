#!/usr/bin/env python3
"""
Extract files from ROM by ID
"""

import json
import sys
import zlib
from pathlib import Path

def decompress_goldeneye(data: bytes, compression_type: str) -> bytes:
    """Decompress Goldeneye/PerfectDark format"""
    if len(data) < 4:
        return data
    
    try:
        if compression_type == 'goldeneye':
            # GE: skip first 2 bytes, then zlib
            return zlib.decompress(data[2:], wbits=-15)
        elif compression_type == 'perfect_dark':
            # PD: skip first 5 bytes, then zlib
            return zlib.decompress(data[5:], wbits=-15)
        else:
            return data
    except Exception as e:
        # Not compressed or different format
        return data

def extract_files(rom_path: Path, scan_json: Path, output_dir: Path, file_ids: list):
    """Extract specific files from ROM"""
    
    # Load scan
    with open(scan_json) as f:
        scan = json.load(f)
    
    # Index by ID
    files_by_id = {f['id']: f for f in scan['files']}
    
    # Open ROM
    with open(rom_path, 'rb') as rom:
        for fid in file_ids:
            if fid not in files_by_id:
                print(f"File ID {fid} not found")
                continue
            
            f = files_by_id[fid]
            offset = int(f['rom_offset'], 16)
            size = f.get('compressed_size', f.get('size', 0))
            compressed = f.get('compressed', False)
            compression_type = f.get('compression_type', 'goldeneye')
            
            rom.seek(offset)
            data = rom.read(size)
            
            # Decompress if needed
            if compressed:
                decompressed = decompress_goldeneye(data, compression_type)
                ext = 'bin'
            else:
                decompressed = data
                ext = 'bin'
            
            # Determine filename
            name = f.get('name', f'file_{fid:04d}')
            if not name.endswith('.bin'):
                name = f"{name}.bin"
            
            # Write files
            output_dir.mkdir(parents=True, exist_ok=True)
            
            # Write compressed
            cmp_path = output_dir / f"{fid:04d}_{name}"
            with open(cmp_path, 'wb') as out:
                out.write(data)
            
            # Write decompressed if different
            if data != decompressed:
                dec_path = output_dir / f"{fid:04d}_dec_{name}"
                with open(dec_path, 'wb') as out:
                    out.write(decompressed)
                print(f"Extracted ID {fid:4d}: {name} (cmp={len(data):6d} dec={len(decompressed):6d})")
            else:
                print(f"Extracted ID {fid:4d}: {name} ({len(data):6d} bytes)")

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("Usage: extract_files.py <rom.z64> <scan.json> <output_dir> <id1> [id2] [id3] ...")
        sys.exit(1)
    
    rom_path = Path(sys.argv[1])
    scan_json = Path(sys.argv[2])
    output_dir = Path(sys.argv[3])
    file_ids = [int(x) for x in sys.argv[4:]]
    
    extract_files(rom_path, scan_json, output_dir, file_ids)
