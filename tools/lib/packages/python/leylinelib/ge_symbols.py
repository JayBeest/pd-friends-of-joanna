#!/usr/bin/env python3
"""
Goldeneye Symbol Extraction

Extracts symbol tables from Goldeneye decomp ELF builds for use in ROM patching.
Similar to the old pd.json approach used for mouse injector.
"""

import subprocess
import json
import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class SymbolExtractor:
    """Extract symbols from ELF files built by GE/PD decomp projects."""
    
    def __init__(self, elf_path: str):
        self.elf_path = Path(elf_path)
        if not self.elf_path.exists():
            raise FileNotFoundError(f"ELF file not found: {elf_path}")
    
    def extract_symbols(self) -> Dict[str, any]:
        """
        Extract all symbols from ELF file.
        
        Returns dict with structure:
        {
            "functions": {"80001234": "funcName", ...},
            "variables": {"80005678": "varName", ...},
            "metadata": {"elf_path": "...", "total_symbols": 123}
        }
        """
        # Use nm to extract symbols
        # Format: ADDRESS TYPE NAME
        try:
            result = subprocess.run(
                ['mips-linux-gnu-nm', '-n', str(self.elf_path)],
                capture_output=True,
                text=True,
                check=True
            )
            nm_output = result.stdout
        except (subprocess.CalledProcessError, FileNotFoundError):
            # Try alternative toolchain
            try:
                result = subprocess.run(
                    ['mips64-linux-gnu-nm', '-n', str(self.elf_path)],
                    capture_output=True,
                    text=True,
                    check=True
                )
                nm_output = result.stdout
            except (subprocess.CalledProcessError, FileNotFoundError):
                # Try without prefix
                result = subprocess.run(
                    ['nm', '-n', str(self.elf_path)],
                    capture_output=True,
                    text=True,
                    check=True
                )
                nm_output = result.stdout
        
        functions = {}
        variables = {}
        
        for line in nm_output.strip().split('\n'):
            if not line.strip():
                continue
            
            parts = line.split()
            if len(parts) < 3:
                continue
            
            address, sym_type, name = parts[0], parts[1], parts[2]
            
            # Convert address to format used in pd.json (0x80XXXXXX or 0x70XXXXXX)
            try:
                addr_int = int(address, 16)
                # Format as 8-digit hex with appropriate prefix
                if addr_int >= 0x80000000:
                    addr_str = f"{addr_int:08x}"
                elif addr_int >= 0x70000000:
                    addr_str = f"{addr_int:08x}"
                else:
                    # Skip addresses outside game memory
                    continue
            except ValueError:
                continue
            
            # Categorize by symbol type
            # T/t = text (code), D/d/B/b = data/bss (variables)
            if sym_type in ['T', 't']:
                functions[addr_str] = name
            elif sym_type in ['D', 'd', 'B', 'b', 'R', 'r']:
                variables[addr_str] = name
        
        metadata = {
            "elf_path": str(self.elf_path),
            "total_symbols": len(functions) + len(variables),
            "functions": len(functions),
            "variables": len(variables)
        }
        
        return {
            "functions": functions,
            "variables": variables,
            "metadata": metadata
        }
    
    def extract_to_json(self, output_path: Optional[str] = None) -> str:
        """
        Extract symbols and save as JSON.
        
        Returns path to output file.
        """
        symbols = self.extract_symbols()
        
        if output_path is None:
            # Default to same dir as ELF with .symbols.json extension
            output_path = self.elf_path.with_suffix('.symbols.json')
        
        output_path = Path(output_path)
        
        with open(output_path, 'w') as f:
            json.dump(symbols, f, indent=2)
        
        return str(output_path)


class SymbolResolver:
    """Resolve addresses to symbol names from extracted symbol files."""
    
    def __init__(self, symbols_json: str):
        self.symbols_path = Path(symbols_json)
        if not self.symbols_path.exists():
            raise FileNotFoundError(f"Symbols file not found: {symbols_json}")
        
        with open(self.symbols_path) as f:
            data = json.load(f)
        
        self.functions = data.get('functions', {})
        self.variables = data.get('variables', {})
        self.metadata = data.get('metadata', {})
        
        # Create reverse lookup: name -> address
        self.name_to_addr = {}
        for addr, name in self.functions.items():
            self.name_to_addr[name] = addr
        for addr, name in self.variables.items():
            self.name_to_addr[name] = addr
    
    def resolve_address(self, address: int) -> Optional[str]:
        """
        Look up symbol name by address.
        
        Args:
            address: Integer address (e.g. 0x80001234)
        
        Returns:
            Symbol name if found, None otherwise
        """
        addr_str = f"{address:08x}"
        
        # Try functions first, then variables
        if addr_str in self.functions:
            return self.functions[addr_str]
        if addr_str in self.variables:
            return self.variables[addr_str]
        
        return None
    
    def resolve_name(self, name: str) -> Optional[int]:
        """
        Look up address by symbol name.
        
        Args:
            name: Symbol name (e.g. "mainLoop")
        
        Returns:
            Integer address if found, None otherwise
        """
        addr_str = self.name_to_addr.get(name)
        if addr_str:
            return int(addr_str, 16)
        return None
    
    def find_functions_by_pattern(self, pattern: str) -> List[Tuple[int, str]]:
        """
        Find functions matching a regex pattern.
        
        Returns list of (address, name) tuples.
        """
        regex = re.compile(pattern)
        matches = []
        
        for addr_str, name in self.functions.items():
            if regex.search(name):
                matches.append((int(addr_str, 16), name))
        
        return sorted(matches)
    
    def get_stats(self) -> Dict[str, any]:
        """Get statistics about loaded symbols."""
        return {
            "source": str(self.symbols_path),
            "total_symbols": len(self.functions) + len(self.variables),
            "functions": len(self.functions),
            "variables": len(self.variables),
            "metadata": self.metadata
        }


def extract_symbols_cli(elf_path: str, output_path: Optional[str] = None, 
                        format: str = 'json', verbose: bool = False) -> None:
    """
    CLI interface for symbol extraction.
    
    Args:
        elf_path: Path to ELF file
        output_path: Optional output path
        format: Output format ('json' or 'compact')
        verbose: Print verbose output
    """
    extractor = SymbolExtractor(elf_path)
    symbols = extractor.extract_symbols()
    
    if verbose:
        print(f"Extracted {symbols['metadata']['total_symbols']} symbols:")
        print(f"  Functions: {symbols['metadata']['functions']}")
        print(f"  Variables: {symbols['metadata']['variables']}")
    
    if format == 'compact':
        # Compact format like old pd.json (just functions dict)
        output_data = symbols['functions']
    else:
        # Full format with metadata
        output_data = symbols
    
    if output_path:
        with open(output_path, 'w') as f:
            json.dump(output_data, f, indent=2)
        if verbose:
            print(f"Saved to: {output_path}")
    else:
        # Print to stdout
        print(json.dumps(output_data, indent=2))
