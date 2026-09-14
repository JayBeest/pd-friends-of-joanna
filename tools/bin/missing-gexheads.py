#!/usr/bin/env python3
"""
Generate JSON snippets for GEX Chead entries.
Extracts names from entries like 'gex.z64:Cheaddark_combatZ' and creates structured output.
"""

import json

def extract_name(entry: str) -> str:
    """
    Extract the name portion from a GEX entry.
    
    Args:
        entry: Entry string like 'gex.z64:Cheaddark_combatZ'
        
    Returns:
        The extracted name without prefix and suffix (e.g., 'dark_combat')
    """
    # Remove 'gex.z64:Chead' from the beginning
    if not entry.startswith('gex.z64:Chead'):
        raise ValueError(f"Entry does not start with expected prefix: {entry}")
    
    name_part = entry[len('gex.z64:Chead'):]  # Get everything after 'gex.z64:Chead'
    
    # Remove trailing 'Z' if present
    if name_part.endswith('Z'):
        name_part = name_part[:-1]
    
    return name_part

def generate_json_entry(entry: str) -> dict:
    """
    Generate a JSON entry from a GEX entry string.
    
    Args:
        entry: Entry string like 'gex.z64:Cheaddark_combatZ'
        
    Returns:
        Dictionary with name, description, source, and prefix fields
    """
    # Extract the base name (e.g., 'dark_combat')
    name = extract_name(entry)
    
    # Construct the full name for output
    full_name = f"Cheadgex{name}Z"
    
    return {
        "name": full_name,
        "description": f"{name}; gex slot Chead{name}Z",
        "source": {
            "rom": "gex",
            "lookup": "byName",
            "alias": f"Chead{name}Z"
        },
        "prefix": "cs only"
    }

def main():
    # List of GEX entries to process
    entries = [
        'gex.z64:Cheaddark_combatZ',
        'gex.z64:CheadelvisZ',
        'gex.z64:CheadcarringtonZ'
        'gex.z64:CheadddshockZ',
        'gex.z64:CheadrussZ',
        'gex.z64:CheadgreyZ',
        'gex.z64:CheaddarlingZ',  
        'gex.z64:CheadbeauZ',  # 
        'gex.z64:Cheadduncan2Z',  
        'gex.z64:CheadedmcgZ',  # 
        'gex.z64:Cheadmatt_cZ',
        'gex.z64:CheadjonathanZ',
        'gex.z64:Cheadmaian_sZ',
        'gex.z64:CheadshaunZ',  # Note trailing space
        'gex.z64:CheaddarkaquaZ',
        'gex.z64:CheadddsniperZ',
        'gex.z64:CheadmotoZ',
        'gex.z64:CheadstevemZ',
        'gex.z64:CheadpennyZ'
    ]
    
    # Process each entry and generate JSON snippets
    json_entries = []
    for entry in entries:
        try:
            json_entry = generate_json_entry(entry)
            json_entries.append(json_entry)
        except ValueError as e:
            print(f"Warning: Skipping invalid entry '{entry}': {e}")
    
    # Print all JSON snippets (one per line, formatted for readability)
    for i, entry in enumerate(json_entries):
        # print(f"\n--- Entry {i + 1}/{len(entries)} ---")
        print(json.dumps(entry, indent=2))

if __name__ == "__main__":
    main()
