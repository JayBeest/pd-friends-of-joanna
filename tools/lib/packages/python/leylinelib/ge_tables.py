#!/usr/bin/env python3
"""
Goldeneye Character Table Analyzer
==================================

Analyzes head and body tables in Goldeneye ROMs to detect custom additions.

Tables analyzed:
- intro_char_table: Credits screen characters
- random_male_heads: MP randomization pool (male)
- random_female_heads: MP randomization pool (female)
"""

import struct
import json
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class GETableAnalyzer:
    """Analyze Goldeneye character tables"""
    
    # Known table addresses (RAM) from symbols
    TABLE_ADDRESSES = {
        'intro_char_table': 0x8002b600,
        'random_male_heads': 0x8002cdb8,
        'random_female_heads': 0x8002ce20,
    }
    
    # Vanilla counts for comparison
    VANILLA_COUNTS = {
        'intro_char_table': 72,
        'random_male_heads': 27,
        'random_female_heads': 4,
    }
    
    # N64 memory layout
    RAM_BASE = 0x80000000
    ROM_OFFSET = 0x1000  # After ROM header
    
    def __init__(self, rom_path: str, symbols_file: Optional[str] = None):
        """
        Initialize table analyzer
        
        Args:
            rom_path: Path to GE ROM file
            symbols_file: Optional symbols JSON (can override default addresses)
        """
        self.rom_path = Path(rom_path)
        if not self.rom_path.exists():
            raise FileNotFoundError(f"ROM not found: {rom_path}")
        
        with open(self.rom_path, 'rb') as f:
            self.rom_data = f.read()
        
        # Load symbol overrides if provided
        self.symbols = {}
        if symbols_file:
            self._load_symbols(symbols_file)
    
    def _load_symbols(self, symbols_file: str):
        """Load symbols from JSON file"""
        try:
            with open(symbols_file, 'r') as f:
                data = json.load(f)
            
            # Convert address strings to ints
            for addr_str, name in data.items():
                addr = int(addr_str, 16)
                self.symbols[name] = addr
            
            print(f"Loaded {len(self.symbols)} symbols")
        except Exception as e:
            print(f"Warning: Could not load symbols: {e}")
    
    def ram_to_rom(self, ram_addr: int) -> int:
        """
        Convert N64 RAM address to ROM offset
        
        Args:
            ram_addr: RAM address (0x80xxxxxx)
        
        Returns:
            ROM file offset
        """
        return ram_addr - self.RAM_BASE + self.ROM_OFFSET
    
    def get_table_address(self, table_name: str) -> int:
        """Get RAM address for table (from symbols or defaults)"""
        if table_name in self.symbols:
            return self.symbols[table_name]
        return self.TABLE_ADDRESSES.get(table_name, 0)
    
    def read_s32(self, offset: int) -> int:
        """Read signed 32-bit int from ROM (big-endian)"""
        return struct.unpack('>i', self.rom_data[offset:offset+4])[0]
    
    def read_u32(self, offset: int) -> int:
        """Read unsigned 32-bit int from ROM (big-endian)"""
        return struct.unpack('>I', self.rom_data[offset:offset+4])[0]
    
    def parse_intro_char_table(self) -> List[Dict]:
        """
        Parse intro_char_table (credits characters)
        
        Structure per entry (28 bytes):
          s32 body_id
          s32 head_id
          s32 str_id1
          s32 str_id2
          s32 str_id3
          s32 flag1
          s32 flag2
        
        Returns:
            List of character entries
        """
        ram_addr = self.get_table_address('intro_char_table')
        rom_offset = self.ram_to_rom(ram_addr)
        
        entries = []
        offset = rom_offset
        entry_size = 28  # 7 * 4 bytes
        
        # Parse until we find invalid entry (body < 0)
        while offset + entry_size <= len(self.rom_data):
            body_id = self.read_s32(offset)
            head_id = self.read_s32(offset + 4)
            
            # Check for terminator
            if body_id < 0:
                break
            
            entries.append({
                'body_id': body_id,
                'head_id': head_id,
                'str_id1': self.read_s32(offset + 8),
                'str_id2': self.read_s32(offset + 12),
                'str_id3': self.read_s32(offset + 16),
                'flag1': self.read_s32(offset + 20),
                'flag2': self.read_s32(offset + 24),
            })
            
            offset += entry_size
        
        return entries
    
    def parse_random_heads(self, table_name: str) -> List[int]:
        """
        Parse random_male_heads or random_female_heads
        
        Simple s32 array terminated with -1
        
        Args:
            table_name: 'random_male_heads' or 'random_female_heads'
        
        Returns:
            List of head IDs
        """
        ram_addr = self.get_table_address(table_name)
        rom_offset = self.ram_to_rom(ram_addr)
        
        heads = []
        offset = rom_offset
        
        while offset + 4 <= len(self.rom_data):
            head_id = self.read_s32(offset)
            
            # -1 terminates the array
            if head_id == -1:
                break
            
            heads.append(head_id)
            offset += 4
        
        return heads
    
    def analyze_all_tables(self) -> Dict:
        """
        Analyze all character tables and compare to vanilla
        
        Returns:
            Analysis results for all tables
        """
        results = {}
        
        # Analyze intro_char_table
        intro_chars = self.parse_intro_char_table()
        vanilla_intro = self.VANILLA_COUNTS['intro_char_table']
        results['intro_char_table'] = {
            'ram_address': f"0x{self.get_table_address('intro_char_table'):08x}",
            'rom_offset': f"0x{self.ram_to_rom(self.get_table_address('intro_char_table')):08x}",
            'vanilla_count': vanilla_intro,
            'found_count': len(intro_chars),
            'modified': len(intro_chars) != vanilla_intro,
            'new_count': max(0, len(intro_chars) - vanilla_intro),
            'entries': intro_chars,
        }
        
        # Analyze random_male_heads
        male_heads = self.parse_random_heads('random_male_heads')
        vanilla_male = self.VANILLA_COUNTS['random_male_heads']
        results['random_male_heads'] = {
            'ram_address': f"0x{self.get_table_address('random_male_heads'):08x}",
            'rom_offset': f"0x{self.ram_to_rom(self.get_table_address('random_male_heads')):08x}",
            'vanilla_count': vanilla_male,
            'found_count': len(male_heads),
            'modified': len(male_heads) != vanilla_male,
            'new_count': max(0, len(male_heads) - vanilla_male),
            'head_ids': male_heads,
        }
        
        # Analyze random_female_heads
        female_heads = self.parse_random_heads('random_female_heads')
        vanilla_female = self.VANILLA_COUNTS['random_female_heads']
        results['random_female_heads'] = {
            'ram_address': f"0x{self.get_table_address('random_female_heads'):08x}",
            'rom_offset': f"0x{self.ram_to_rom(self.get_table_address('random_female_heads')):08x}",
            'vanilla_count': vanilla_female,
            'found_count': len(female_heads),
            'modified': len(female_heads) != vanilla_female,
            'new_count': max(0, len(female_heads) - vanilla_female),
            'head_ids': female_heads,
        }
        
        return results
    
    def print_summary(self, results: Dict):
        """Print human-readable summary of analysis"""
        print("\n" + "=" * 60)
        print("Goldeneye Character Table Analysis")
        print("=" * 60)
        print(f"ROM: {self.rom_path.name}\n")
        
        total_modifications = 0
        
        for table_name, data in results.items():
            status = "⚠ MODIFIED" if data['modified'] else "✓ Unmodified"
            
            print(f"{table_name} ({data['ram_address']}):")
            print(f"  ROM Offset: {data['rom_offset']}")
            print(f"  Vanilla: {data['vanilla_count']} entries")
            print(f"  Found:   {data['found_count']} entries")
            print(f"  Status:  {status}")
            
            if data['modified']:
                total_modifications += 1
                if data['new_count'] > 0:
                    print(f"  New:     +{data['new_count']} entries added")
                else:
                    print(f"  Changed: {data['vanilla_count'] - data['found_count']} entries removed")
                
                # Show details for small tables
                if table_name.startswith('random_') and data['new_count'] > 0:
                    print(f"  New head IDs:")
                    for i, head_id in enumerate(data['head_ids'][data['vanilla_count']:]):
                        print(f"    [{data['vanilla_count'] + i}] ID={head_id} (0x{head_id:02x})")
            
            print()
        
        print(f"Summary: {total_modifications}/3 tables modified")
        print("=" * 60)


def analyze_ge_tables(rom_path: str, symbols_file: Optional[str] = None, 
                     output_json: Optional[str] = None) -> Dict:
    """
    Analyze Goldeneye character tables
    
    Args:
        rom_path: Path to GE ROM
        symbols_file: Optional symbols JSON
        output_json: Optional output path for JSON results
    
    Returns:
        Analysis results dictionary
    """
    analyzer = GETableAnalyzer(rom_path, symbols_file)
    results = analyzer.analyze_all_tables()
    analyzer.print_summary(results)
    
    if output_json:
        with open(output_json, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\nDetailed results written to: {output_json}")
    
    return results


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: ge_tables.py <rom_path> [symbols.json] [output.json]")
        print("\nExample:")
        print("  ge_tables.py ge007.u.z64")
        print("  ge_tables.py ge007.u.z64 ge_symbols.json")
        print("  ge_tables.py ge007.u.z64 ge_symbols.json analysis.json")
        sys.exit(1)
    
    rom_path = sys.argv[1]
    symbols_file = sys.argv[2] if len(sys.argv) > 2 else None
    output_json = sys.argv[3] if len(sys.argv) > 3 else None
    
    analyze_ge_tables(rom_path, symbols_file, output_json)
