#!/usr/bin/env python3
"""
ROM Path - A pathlib.Path-like interface for navigating patched ROM files

Provides filesystem-like access to ROM contents with xdelta patch support,
specifically designed for Perfect Dark but extensible to other ROM formats.

Attribution
-----------
The ROM file-table walking and Perfect Dark-specific path semantics
are derived from Ryan Dwyer's pd-extract scripts (perfect-dark decomp
tooling). The pathlib facade is our addition.
"""

import os
import zlib
import subprocess
from pathlib import Path, PurePath
from typing import Optional, Union, Dict, List, BinaryIO
import tempfile
import struct


class ROMPatchError(Exception):
    """Raised when ROM patching operations fail"""
    pass


class ROMPath:
    """
    A pathlib.Path-like interface for ROM files with optional xdelta patch support.
    
    Provides filesystem-like navigation of ROM content with configurable segment detection.
    Supports arbitrary ROM formats through customizable segment definitions.
    """
    
    def __init__(self, rom_file: Path, patch_file: Path = None, segment_config: dict = None):
        self.rom_file = Path(rom_file)
        self.patch_file = Path(patch_file) if patch_file else None
        self.segment_config = segment_config or self._get_default_segment_config()
        self._rom_data = None
        self._segments = None
        self._rom_type = None
    
    def _get_default_segment_config(self) -> dict:
        """Get default segment configuration for known ROM types"""
        return {
            'perfect_dark': {
                'detector': self._detect_perfect_dark,
                'segments': {
                    'files': {'offset': 0x28080, 'type': 'raw'},
                    'data': {'offset': 0x39850, 'type': 'compressed_pd'},
                    'game': {'offset': 0x4fc40, 'type': 'raw'},
                    'animations': {'offset': 0x9dca0, 'type': 'raw'},
                    'mpconfigs': {'offset': 0x15bac80, 'type': 'raw'},
                    'textures': {'offset': 0x1d65f40, 'type': 'texture_table'},
                    'copyright': {'offset': 0x1fd0000, 'type': 'raw'}
                }
            },
            'generic': {
                'detector': lambda data: True,  # Always matches as fallback
                'segments': {}  # Empty - requires manual configuration
            }
        }
    
    @property
    def rom_data(self) -> bytes:
        """Get ROM data, applying patch if needed"""
        if self._rom_data is None:
            if self.patch_file and self.patch_file.exists():
                self._rom_data = self._apply_xdelta_patch()
            else:
                with open(self.rom_file, 'rb') as f:
                    self._rom_data = f.read()
        return self._rom_data
    
    def _detect_perfect_dark(self, data: bytes) -> bool:
        """Detect Perfect Dark ROM (or PD-format derivative such as GE-X).

        Modded PD ROMs rebrand the cartridge title (e.g. "GoldenEye X"),
        so a title-only check is too strict. We accept any ROM that:
          - matches the standard PD title, OR
          - exposes the PD-format compressed data segment magic 0x1173
            at one of the known PD per-variant data offsets.
        """
        if len(data) < 0x34:
            return False

        # Fast-path: stock PD title.
        title_bytes = data[0x20:0x30]
        try:
            title = title_bytes.decode('ascii').strip()
            if title.lower() == "perfect dark":
                return True
        except UnicodeDecodeError:
            pass

        # Content-based fallback for modded / rebranded PD ROMs.
        # Any of the per-variant data segment offsets carrying 0x1173
        # is a strong signal this is a PD-format ROM.
        candidate_data_offsets = (0x30850, 0x39850)
        for off in candidate_data_offsets:
            if off + 2 <= len(data) and data[off:off + 2] == b'\x11\x73':
                return True
        return False
    
    def detect_rom_type(self) -> str:
        """Detect the type of ROM using configured detectors"""
        if self._rom_type is not None:
            return self._rom_type
        
        data = self.rom_data
        
        # Try each configured detector
        for rom_type, config in self.segment_config.items():
            if rom_type == 'generic':
                continue  # Skip generic as fallback
            
            detector = config.get('detector')
            if detector and detector(data):
                self._rom_type = rom_type
                return rom_type
        
        self._rom_type = "generic"
        return "generic"
    
    def add_custom_segment(self, name: str, offset: int, segment_type: str = 'raw'):
        """Add a custom segment definition for arbitrary ROMs"""
        rom_type = self.detect_rom_type()
        if rom_type not in self.segment_config:
            self.segment_config[rom_type] = {'segments': {}}
        
        if 'segments' not in self.segment_config[rom_type]:
            self.segment_config[rom_type]['segments'] = {}
        
        self.segment_config[rom_type]['segments'][name] = {
            'offset': offset,
            'type': segment_type
        }
        
        # Clear cached segments to force re-parsing
        self._segments = None
    
    def configure_segments(self, segments: dict):
        """Configure segments for generic ROM support"""
        rom_type = self.detect_rom_type()
        if rom_type not in self.segment_config:
            self.segment_config[rom_type] = {}
        
        self.segment_config[rom_type]['segments'] = segments
        self._segments = None
    
    def _apply_xdelta_patch(self) -> bytes:
        """Apply xdelta patch to ROM data"""
        import subprocess
        import tempfile
        
        try:
            with tempfile.NamedTemporaryFile() as patched_rom:
                # Run xdelta3 to apply patch
                result = subprocess.run([
                    'xdelta3', '-d', 
                    str(self.patch_file),  # patch file
                    str(self.rom_file),    # source file
                    patched_rom.name       # output file
                ], capture_output=True, text=True)
                
                if result.returncode != 0:
                    raise ROMPatchError(f"xdelta patch failed: {result.stderr}")
                
                # Read patched ROM data
                patched_rom.seek(0)
                return patched_rom.read()
                
        except FileNotFoundError:
            raise ROMPatchError("xdelta3 tool not found. Please install xdelta3.")
        except Exception as e:
            raise ROMPatchError(f"Error applying patch: {e}")
    
    def stat(self) -> 'ROMStat':
        """Get ROM statistics"""
        data = self.rom_data
        return ROMStat(
            size=len(data),
            rom_type=self.detect_rom_type(),
            patched=self.patch_file is not None
        )
    
    def segments(self) -> list:
        """List available ROM segments"""
        rom_type = self.detect_rom_type()
        if rom_type in self.segment_config:
            return list(self.segment_config[rom_type].get('segments', {}).keys())
        return []
    
    def __truediv__(self, segment_name: str) -> 'ROMPathSegment':
        """Navigate to a ROM segment using / operator"""
        return ROMPathSegment(self, segment_name)
    
    def __str__(self) -> str:
        patch_info = f" (patched)" if self.patch_file else ""
        return f"ROMPath({self.rom_file.name}{patch_info})"
    
    def __repr__(self) -> str:
        return f"ROMPath({self.rom_file!r}, patch={self.patch_file!r})"
    
    def extract_segment(self, segment_name: str) -> bytes:
        """Extract data from a specific ROM segment"""
        rom_type = self.detect_rom_type()
        
        if rom_type not in self.segment_config:
            raise ValueError(f"No segment configuration for ROM type: {rom_type}")
        
        segments = self.segment_config[rom_type].get('segments', {})
        if segment_name not in segments:
            raise ValueError(f"Unknown segment: {segment_name}")
        
        segment_info = segments[segment_name]
        offset = segment_info['offset']
        segment_type = segment_info.get('type', 'raw')
        
        data = self.rom_data
        
        if segment_type == 'compressed_pd':
            # Find compressed data length by looking for next segment or end
            next_offset = len(data)
            for other_segment in segments.values():
                other_offset = other_segment['offset']
                if other_offset > offset and other_offset < next_offset:
                    next_offset = other_offset
            
            compressed_data = data[offset:next_offset]
            return decompress_pd_data(compressed_data)
        
        elif segment_type == 'texture_table':
            # Extract texture segment with table parsing
            return self._extract_texture_segment(data, offset)
        
        else:  # raw or unknown type
            # Find segment end by looking for next segment or file end
            next_offset = len(data)
            for other_segment in segments.values():
                other_offset = other_segment['offset']
                if other_offset > offset and other_offset < next_offset:
                    next_offset = other_offset
            
            return data[offset:next_offset]
    
    def _extract_texture_segment(self, data: bytes, offset: int) -> bytes:
        """Extract texture segment data"""
        # For now, extract a reasonable chunk around texture data
        # This can be refined with better texture table parsing
        end_offset = min(offset + 0x300000, len(data))  # 3MB max
        return data[offset:end_offset]


class ROMPatchError(Exception):
    """Exception raised when ROM patching fails"""
    pass


class ROMStat:
    """Statistics about ROM file, similar to os.stat_result"""
    
    def __init__(self, size: int, rom_type: str = "unknown", patched: bool = False):
        self.st_size = size
        self.rom_type = rom_type
        self.patched = patched
    
    def __repr__(self) -> str:
        return f"ROMStat(size={self.st_size}, rom_type='{self.rom_type}', patched={self.patched})"


class ROMPathSegment:
    """Represents a path within a ROM segment"""
    
    def __init__(self, rom_path: ROMPath, segment_path: str):
        self.rom_path = rom_path
        self.segment_path = segment_path
        self._parse_path()
    
    def _parse_path(self):
        """Parse segment path into components"""
        parts = self.segment_path.split('/')
        self.segment_name = parts[0] if parts else ""
        self.sub_path = '/'.join(parts[1:]) if len(parts) > 1 else ""
    
    def __str__(self) -> str:
        return f"{self.rom_path}/{self.segment_path}"
    
    def __repr__(self) -> str:
        return f"ROMPathSegment({self.rom_path!r}, {self.segment_path!r})"
    
    def __truediv__(self, path_component: str) -> 'ROMPathSegment':
        """Navigate deeper into segment"""
        new_path = f"{self.segment_path}/{path_component}"
        return ROMPathSegment(self.rom_path, new_path)
    
    def exists(self) -> bool:
        """Check if segment/path exists"""
        try:
            segments = self.rom_path.segments()
            return self.segment_name in segments
        except:
            return False
    
    def read_bytes(self) -> bytes:
        """Read segment data as bytes"""
        return self.rom_path.extract_segment(self.segment_name)
    
    def open(self, mode: str = 'rb') -> BinaryIO:
        """Open segment as file-like object"""
        if 'b' not in mode:
            raise ValueError("Only binary mode supported for ROM data")
        
        import io
        data = self.read_bytes()
        return io.BytesIO(data)
    
    def stat(self) -> 'ROMStat':
        """Get segment statistics"""
        data = self.read_bytes()
        return ROMStat(size=len(data), rom_type="segment")
    
    def extract_to(self, output_path: Union[str, Path]) -> Path:
        """Extract segment to file"""
        output_path = Path(output_path)
        data = self.read_bytes()
        
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'wb') as f:
            f.write(data)
        
        return output_path


class ROMStat:
    """ROM file statistics (similar to os.stat_result)"""
    
    def __init__(self, size: int, rom_type: str = "unknown", patched: bool = False):
        self.st_size = size
        self.rom_type = rom_type  
        self.patched = patched
    
    def __repr__(self) -> str:
        return f"ROMStat(size={self.st_size}, type={self.rom_type}, patched={self.patched})"


# Utility functions for ROM manipulation
def decompress_pd_data(data: bytes) -> bytes:
    """Decompress Perfect Dark compressed data (from extract tool)"""
    if len(data) < 5:
        return data
    
    header = struct.unpack('>H', data[0:2])[0]
    if header == 0x1173:
        return zlib.decompress(data[5:], wbits=-15)
    
    return data  # Not compressed


def list_pd_textures(rom_path: ROMPath) -> List[Dict]:
    """List all textures in Perfect Dark ROM"""
    textures_segment = rom_path / "textures"
    texture_data = textures_segment.read_bytes()
    
    # Texture table parsing (based on extract tool)
    datalen = 0x291d60  # ntsc-final
    tablepos = datalen
    textures = []
    index = 0
    
    while tablepos < len(texture_data) - 16:
        start = struct.unpack('>I', b'\x00' + texture_data[tablepos+1:tablepos+4])[0]
        end = struct.unpack('>I', b'\x00' + texture_data[tablepos+9:tablepos+12])[0]
        
        if struct.unpack('>I', texture_data[tablepos+12:tablepos+16])[0] != 0:
            break
        
        textures.append({
            'index': index,
            'start': start,
            'end': end,
            'size': end - start,
            'filename': f'{index:04x}.bin'
        })
        
        index += 1
        tablepos += 8
    
    return textures


# Example usage and testing
if __name__ == "__main__":
    # Example usage
    print("ROM Path - Perfect Dark ROM Navigator")
    print("====================================")
    
    # This would be used like:
    # rom = ROMPath("pd.ntsc-final.z64", "friends_of_joanna.xdelta")
    # print(f"ROM info: {rom.stat()}")
    # print(f"Available segments: {rom.segments()}")
    # 
    # # Navigate like pathlib
    # textures = rom / "textures"
    # game_binary = rom / "game"
    # 
    # # Extract content
    # texture_data = textures.read_bytes()
    # with (rom / "animations").open('rb') as f:
    #     anim_data = f.read()
    # 
    # # Extract to filesystem
    # textures.extract_to("extracted/textures.bin")