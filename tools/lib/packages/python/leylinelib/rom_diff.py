#!/usr/bin/env python3
"""
Compare two ROM scans to find modified/added files
"""

import json
import sys
from pathlib import Path
from typing import Dict, List, Set

def load_scan(path: Path) -> dict:
    """Load a ROM scan JSON file"""
    with open(path, 'r') as f:
        return json.load(f)

def compare_scans(vanilla_path: Path, modded_path: Path, output_path: Path):
    """Compare two ROM scans and write differences to file"""
    vanilla = load_scan(vanilla_path)
    modded = load_scan(modded_path)
    
    # Index files by hash for vanilla
    vanilla_by_hash: Dict[str, dict] = {}
    vanilla_by_name: Dict[str, dict] = {}
    
    for f in vanilla['files']:
        h = f.get('sha256')
        if h:
            vanilla_by_hash[h] = f
        name = f.get('name', '')
        if name:
            vanilla_by_name[name] = f
    
    # Categorize modded files
    new_files = []
    modified_files = []
    unchanged_files = []
    
    for f in modded['files']:
        h = f.get('sha256')
        name = f.get('name', '')
        
        # Check if this file exists in vanilla
        if h and h in vanilla_by_hash:
            unchanged_files.append(f)
        elif name and name in vanilla_by_name:
            # Same name but different hash = modified
            vanilla_file = vanilla_by_name[name]
            modified_files.append({
                'name': name,
                'category': f.get('category', 'unknown'),
                'id': f['id'],
                'rom_offset': f['rom_offset'],
                'compressed_size': f.get('compressed_size', f.get('size', 0)),
                'uncompressed_size': f.get('uncompressed_size', f.get('size', 0)),
                'vanilla_hash': vanilla_file.get('sha256', 'unknown'),
                'modded_hash': h
            })
        else:
            # Completely new file
            new_files.append({
                'name': name if name else f'file_{f["id"]:04d}',
                'category': f.get('category', 'unknown'),
                'id': f['id'],
                'rom_offset': f['rom_offset'],
                'compressed_size': f.get('compressed_size', f.get('size', 0)),
                'uncompressed_size': f.get('uncompressed_size', f.get('size', 0)),
                'hash': h
            })
    
    # Focus on character files
    new_heads = [f for f in new_files if 'Chead' in f['name']]
    modified_heads = [f for f in modified_files if 'Chead' in f['name']]
    
    new_chrs = [f for f in new_files if f['category'] == 'chrs']
    modified_chrs = [f for f in modified_files if f['category'] == 'chrs']
    
    # Write report
    with open(output_path, 'w') as out:
        out.write(f"ROM Diff Report\n")
        out.write(f"=" * 80 + "\n")
        out.write(f"Vanilla: {vanilla_path.name}\n")
        out.write(f"Modded:  {modded_path.name}\n")
        out.write(f"\n")
        
        out.write(f"Summary:\n")
        out.write(f"  Total vanilla files: {len(vanilla['files'])}\n")
        out.write(f"  Total modded files:  {len(modded['files'])}\n")
        out.write(f"  New files:           {len(new_files)}\n")
        out.write(f"  Modified files:      {len(modified_files)}\n")
        out.write(f"  Unchanged files:     {len(unchanged_files)}\n")
        out.write(f"\n")
        
        out.write(f"Character Files:\n")
        out.write(f"  New heads:           {len(new_heads)}\n")
        out.write(f"  Modified heads:      {len(modified_heads)}\n")
        out.write(f"  New chrs files:      {len(new_chrs)}\n")
        out.write(f"  Modified chrs files: {len(modified_chrs)}\n")
        out.write(f"\n")
        
        if new_heads:
            out.write(f"\nNew Head Files ({len(new_heads)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(new_heads, key=lambda x: x['name']):
                out.write(f"  {f['name']:<30} ID={f['id']:4d} offset=0x{f['rom_offset']:08x} "
                         f"cmp={f['compressed_size']:6d} dec={f['uncompressed_size']:6d}\n")
        
        if modified_heads:
            out.write(f"\nModified Head Files ({len(modified_heads)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(modified_heads, key=lambda x: x['name']):
                out.write(f"  {f['name']:<30} ID={f['id']:4d} offset=0x{f['rom_offset']:08x}\n")
        
        if new_chrs:
            out.write(f"\nNew Character Files ({len(new_chrs)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(new_chrs, key=lambda x: x['name']):
                out.write(f"  {f['name']:<30} ID={f['id']:4d} offset=0x{f['rom_offset']:08x} "
                         f"cmp={f['compressed_size']:6d} dec={f['uncompressed_size']:6d}\n")
        
        if modified_chrs:
            out.write(f"\nModified Character Files ({len(modified_chrs)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(modified_chrs, key=lambda x: x['name']):
                out.write(f"  {f['name']:<30} ID={f['id']:4d} offset=0x{f['rom_offset']:08x}\n")
        
        # Write full lists
        if len(new_files) > len(new_heads) + len(new_chrs):
            out.write(f"\nAll New Files ({len(new_files)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(new_files, key=lambda x: (x['category'], x['name'])):
                out.write(f"  [{f['category']:10s}] {f['name']:<40} ID={f['id']:4d}\n")
        
        if len(modified_files) > len(modified_heads) + len(modified_chrs):
            out.write(f"\nAll Modified Files ({len(modified_files)}):\n")
            out.write(f"-" * 80 + "\n")
            for f in sorted(modified_files, key=lambda x: (x['category'], x['name'])):
                out.write(f"  [{f['category']:10s}] {f['name']:<40} ID={f['id']:4d}\n")
    
    # Also save JSON for programmatic access
    json_output = output_path.with_suffix('.json')
    with open(json_output, 'w') as f:
        json.dump({
            'vanilla_file': str(vanilla_path),
            'modded_file': str(modded_path),
            'summary': {
                'vanilla_count': len(vanilla['files']),
                'modded_count': len(modded['files']),
                'new_count': len(new_files),
                'modified_count': len(modified_files),
                'unchanged_count': len(unchanged_files)
            },
            'new_heads': new_heads,
            'modified_heads': modified_heads,
            'new_chrs': new_chrs,
            'modified_chrs': modified_chrs,
            'all_new': new_files,
            'all_modified': modified_files
        }, f, indent=2)
    
    print(f"Wrote diff report: {output_path}")
    print(f"Wrote diff JSON: {json_output}")
    print(f"\nFound {len(new_heads)} new heads, {len(modified_heads)} modified heads")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: rom_diff.py <vanilla_scan.json> <modded_scan.json> <output.txt>")
        sys.exit(1)
    
    compare_scans(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
