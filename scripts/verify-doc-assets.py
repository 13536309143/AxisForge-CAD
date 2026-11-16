"""Validate the README screenshot without image-editor dependencies."""
from pathlib import Path
import struct
import zlib

data = Path('docs/images/workbench.png').read_bytes()
assert data.startswith(b'\x89PNG\r\n\x1a\n'), 'Invalid PNG signature'
offset, compressed, saw_header, saw_end = 8, bytearray(), False, False
while offset < len(data):
    assert offset + 12 <= len(data), 'Truncated PNG chunk header'
    length = struct.unpack('>I', data[offset:offset + 4])[0]
    assert offset + length + 12 <= len(data), 'Truncated PNG payload'
    kind = data[offset + 4:offset + 8]
    payload = data[offset + 8:offset + 8 + length]
    expected = struct.unpack('>I', data[offset + 8 + length:offset + 12 + length])[0]
    assert zlib.crc32(kind + payload) & 0xffffffff == expected, 'PNG CRC mismatch'
    if kind == b'IHDR':
        assert length == 13 and offset == 8
        width, height = struct.unpack('>II', payload[:8])
        assert 0 < width <= 20000 and 0 < height <= 20000
        saw_header = True
    if kind == b'IDAT':
        compressed.extend(payload)
    offset += length + 12
    if kind == b'IEND':
        assert length == 0 and offset == len(data), 'Invalid PNG end'
        saw_end = True
assert saw_header and saw_end and compressed, 'Incomplete PNG'
decoder = zlib.decompressobj()
decoded = decoder.decompress(compressed, 64 * 1024 * 1024)
assert decoder.eof and not decoder.unconsumed_tail and decoded, 'Invalid PNG image stream'
print(f'Validated README screenshot: {width} x {height}, {len(data)} bytes')

