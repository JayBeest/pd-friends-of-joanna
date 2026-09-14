#!/usr/bin/env python3
"""
Symbol extraction from ELF files for N64 decomp projects.

This module extracts function and variable symbols from ELF build outputs
(Perfect Dark, Goldeneye, etc.) and provides them in JSON format for use
in ROM patching and address resolution.
"""

import subprocess
import json
import re
import logging
from pathlib import Path
from typing import Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


class SymbolExtractor:
    """Extract symbols from ELF files using nm or objdump."""
    
    def __init__(self, elf_path: str):
        """
        Initialize symbol extractor with path to ELF file.
        
        Args:
            elf_path: Path to ELF file (e.g., ge007.u.elf, pd.ntsc-final.elf)
        """
        self.elf_path = Path(elf_path)
        if not self.elf_path.exists():
            raise FileNotFoundError(f"ELF file not found: {elf_path}")
    
    def extract_with_nm(self) -> Dict[str, str]:
        """
        Extract symbols using nm (preferred method).
        
        Returns:
            Dict mapping addresses to symbol names
        """
        try:
            # Run nm with options:
            # -n: sort by address
            # --defined-only: only show defined symbols
            # --numeric-sort: sort numerically
            result = subprocess.run(
                ['nm', '-n', '--defined-only', str(self.elf_path)],
                capture_output=True,
                text=True,
                check=True
            )
            
            symbols = {}
            # nm output format: <address> <type> <name>
            # Example: 70001050 T tlbInit
            pattern = re.compile(r'^([0-9a-f]+)\s+([A-Z])\s+(.+)$', re.IGNORECASE)
            
            for line in result.stdout.splitlines():
                match = pattern.match(line.strip())
                if match:
                    addr, sym_type, name = match.groups()
                    # Only include relevant symbol types:
                    # T/t = text (code)
                    # D/d = initialized data
                    # B/b = uninitialized data
                    # R/r = read-only data
                    if sym_type.upper() in ['T', 'D', 'B', 'R']:
                        symbols[addr] = name
            
            logger.info(f"Extracted {len(symbols)} symbols using nm")
            return symbols
            
        except subprocess.CalledProcessError as e:
            logger.error(f"nm failed: {e.stderr}")
            raise
        except FileNotFoundError:
            logger.warning("nm not found, falling back to objdump")
            return self.extract_with_objdump()
    
    def extract_with_objdump(self) -> Dict[str, str]:
        """
        Extract symbols using objdump (fallback method).
        
        Returns:
            Dict mapping addresses to symbol names
        """
        try:
            result = subprocess.run(
                ['objdump', '-t', str(self.elf_path)],
                capture_output=True,
                text=True,
                check=True
            )
            
            symbols = {}
            # objdump -t output format varies but typically:
            # <address> <flags> <section> <size> <name>
            pattern = re.compile(r'^([0-9a-f]+)\s+\w+\s+\S+\s+[0-9a-f]+\s+(.+)$', re.IGNORECASE)
            
            for line in result.stdout.splitlines():
                match = pattern.match(line.strip())
                if match:
                    addr, name = match.groups()
                    symbols[addr] = name.strip()
            
            logger.info(f"Extracted {len(symbols)} symbols using objdump")
            return symbols
            
        except subprocess.CalledProcessError as e:
            logger.error(f"objdump failed: {e.stderr}")
            raise
    
    def extract_symbols(self) -> Dict[str, str]:
        """
        Extract symbols using available tool (nm preferred, objdump fallback).
        
        Returns:
            Dict mapping addresses (hex string) to symbol names
        """
        return self.extract_with_nm()
    
    def extract_to_json(self, output_path: Optional[str] = None) -> dict:
        """
        Extract symbols and format as JSON compatible with pd.json format.
        
        Args:
            output_path: Optional path to write JSON file
            
        Returns:
            Dict with 'functions' key containing address->name mappings
        """
        symbols = self.extract_symbols()
        
        # Convert to pd.json format: {"functions": {"<addr>": "<name>", ...}}
        # Sort by address for readability
        sorted_symbols = dict(sorted(symbols.items(), key=lambda x: int(x[0], 16)))
        
        output = {
            "functions": sorted_symbols,
            "metadata": {
                "source": str(self.elf_path.name),
                "symbol_count": len(sorted_symbols)
            }
        }
        
        if output_path:
            with open(output_path, 'w') as f:
                json.dump(output, f, indent=2)
            logger.info(f"Wrote {len(sorted_symbols)} symbols to {output_path}")
        
        return output
    
    def extract_to_simple_json(self, output_path: Optional[str] = None) -> dict:
        """
        Extract symbols in simplified format (just address->name dict).
        
        Args:
            output_path: Optional path to write JSON file
            
        Returns:
            Dict mapping addresses to names
        """
        symbols = self.extract_symbols()
        
        # Sort by address
        sorted_symbols = dict(sorted(symbols.items(), key=lambda x: int(x[0], 16)))
        
        if output_path:
            with open(output_path, 'w') as f:
                json.dump(sorted_symbols, f, indent=2)
            logger.info(f"Wrote {len(sorted_symbols)} symbols to {output_path}")
        
        return sorted_symbols
    
    @staticmethod
    def load_symbols(json_path: str) -> Dict[str, str]:
        """
        Load symbols from JSON file (supports both formats).
        
        Args:
            json_path: Path to symbol JSON file
            
        Returns:
            Dict mapping addresses to symbol names
        """
        with open(json_path, 'r') as f:
            data = json.load(f)
        
        # Support both formats:
        # 1. {"functions": {...}} (pd.json format)
        # 2. Direct address->name dict
        if isinstance(data, dict) and "functions" in data:
            return data["functions"]
        elif isinstance(data, dict):
            return data
        else:
            raise ValueError(f"Invalid symbol file format: {json_path}")
    
    @staticmethod
    def lookup_address(symbols: Dict[str, str], address: int) -> Optional[str]:
        """
        Look up symbol name by address.
        
        Args:
            symbols: Symbol dict from load_symbols()
            address: Address to look up (int or hex string)
            
        Returns:
            Symbol name if found, None otherwise
        """
        # Convert address to hex string (without 0x prefix)
        if isinstance(address, int):
            addr_hex = f"{address:08x}"
        else:
            addr_hex = address.lower().replace('0x', '')
        
        return symbols.get(addr_hex)
    
    @staticmethod
    def lookup_symbol(symbols: Dict[str, str], name: str) -> Optional[str]:
        """
        Look up address by symbol name.
        
        Args:
            symbols: Symbol dict from load_symbols()
            name: Symbol name to find
            
        Returns:
            Address (hex string) if found, None otherwise
        """
        for addr, sym_name in symbols.items():
            if sym_name == name:
                return addr
        return None
    
    @staticmethod
    def find_symbols_by_prefix(symbols: Dict[str, str], prefix: str) -> Dict[str, str]:
        """
        Find all symbols starting with given prefix.
        
        Args:
            symbols: Symbol dict from load_symbols()
            prefix: Prefix to search for
            
        Returns:
            Dict of matching address->name pairs
        """
        return {
            addr: name for addr, name in symbols.items()
            if name.startswith(prefix)
        }


def main():
    """Command-line interface for symbol extraction."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Extract symbols from N64 decomp ELF files'
    )
    parser.add_argument('elf_path', help='Path to ELF file')
    parser.add_argument(
        '-o', '--output',
        help='Output JSON file (default: stdout)'
    )
    parser.add_argument(
        '--simple',
        action='store_true',
        help='Use simple format (address->name dict only)'
    )
    parser.add_argument(
        '--lookup-addr',
        help='Look up symbol by address (hex)'
    )
    parser.add_argument(
        '--lookup-symbol',
        help='Look up address by symbol name'
    )
    parser.add_argument(
        '--prefix',
        help='Find symbols starting with prefix'
    )
    
    args = parser.parse_args()
    
    # Configure logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(levelname)s: %(message)s'
    )
    
    extractor = SymbolExtractor(args.elf_path)
    
    # Handle lookup operations
    if args.lookup_addr or args.lookup_symbol or args.prefix:
        symbols = extractor.extract_symbols()
        
        if args.lookup_addr:
            name = SymbolExtractor.lookup_address(symbols, args.lookup_addr)
            if name:
                print(f"{args.lookup_addr}: {name}")
            else:
                print(f"Symbol not found at address {args.lookup_addr}")
                return 1
        
        if args.lookup_symbol:
            addr = SymbolExtractor.lookup_symbol(symbols, args.lookup_symbol)
            if addr:
                print(f"{args.lookup_symbol}: 0x{addr}")
            else:
                print(f"Symbol '{args.lookup_symbol}' not found")
                return 1
        
        if args.prefix:
            matches = SymbolExtractor.find_symbols_by_prefix(symbols, args.prefix)
            if matches:
                for addr, name in sorted(matches.items(), key=lambda x: int(x[0], 16)):
                    print(f"0x{addr}: {name}")
            else:
                print(f"No symbols found with prefix '{args.prefix}'")
                return 1
        
        return 0
    
    # Extract and output symbols
    if args.simple:
        result = extractor.extract_to_simple_json(args.output)
    else:
        result = extractor.extract_to_json(args.output)
    
    if not args.output:
        print(json.dumps(result, indent=2))
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
