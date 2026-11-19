"""Bounded SAN14 LWC container codec used by offline scenario authoring.

No editor executable or third-party Python module is executed or imported.
"""
from __future__ import annotations
import struct

MAGIC = struct.pack('<I', 440620876)
LIMIT = 32 * 1024 * 1024

def unpack(data: bytes) -> tuple[int, bytes]:
    offset = data.find(MAGIC)
    if offset < 0 or offset + 268 > len(data):
        raise ValueError('Missing LWC scenario payload')
    size, packed = struct.unpack_from('<II', data, offset + 4)
    if not 0 < size <= LIMIT or offset + 268 + packed > len(data):
        raise ValueError('Invalid LWC lengths')
    table = data[offset + 12:offset + 268]
    bits = memoryview(data)[offset + 268:offset + 268 + packed]
    position = 0
    def take(n):
        nonlocal position
        if position + n > len(bits) * 8:
            raise ValueError('Truncated LWC code')
        value = 0
        for _ in range(n):
            value = value * 2 + ((bits[position // 8] >> (7 - position % 8)) & 1)
            position += 1
        return value
    def number():
        width = 1
        while take(1):
            width += 1
            if width > 25:
                raise ValueError('Excessive LWC code')
        return (1 << width) - 2 + take(width)
    out = bytearray()
    while len(out) < size:
        token = number()
        if token < 256:
            out.append(table[token])
        else:
            distance = token - 256
            length = number() + 3
            if not 0 < distance <= len(out) or len(out) + length > size:
                raise ValueError('Invalid LWC back reference')
            for _ in range(length):
                out.append(out[-distance])
    return offset, bytes(out)

def checksum(plain: bytes) -> int:
    return pow(sum(plain) & 65535, 5, 1071514531)

def repack(reference: bytes, plain: bytes) -> bytes:
    """Encode with a small greedy LZ dictionary; retain native header/trailer."""
    offset, original = unpack(reference)
    table = reference[offset + 12:offset + 268]
    if len(set(table)) != 256 or not 0 < len(plain) <= LIMIT:
        raise ValueError('Invalid scenario dictionary or payload')
    inverse = {value: index for index, value in enumerate(table)}
    output = bytearray()
    accumulator = count = 0
    def emit(value, width):
        nonlocal accumulator, count
        accumulator = (accumulator << width) | value
        count += width
        while count >= 8:
            count -= 8
            output.append((accumulator >> count) & 255)
        accumulator &= (1 << count) - 1
    def number(value):
        width = (value + 2).bit_length() - 1
        emit((1 << width) - 2, width)
        emit(value - (1 << width) + 2, width)
    recent = {}
    at = 0
    while at < len(plain):
        key = plain[at:at + 3]
        prior = recent.get(key, -1)
        length = 0
        if prior >= 0 and at - prior <= 65535 and len(key) == 3:
            while length < min(260, len(plain) - at) and plain[prior + length] == plain[at + length]:
                length += 1
        step = length if length >= 3 else 1
        if length >= 3:
            number(256 + at - prior)
            number(length - 3)
        else:
            number(inverse[plain[at]])
        for i in range(at, at + step):
            recent[plain[i:i + 3]] = i
        at += step
    if count:
        output.append(accumulator << (8 - count))
    packed_size = struct.unpack_from('<I', reference, offset + 8)[0]
    segment = MAGIC + struct.pack('<II', len(plain), len(output)) + table + output
    result = bytearray(reference[:offset] + segment + reference[offset + 268 + packed_size:])
    if offset >= 467 and struct.unpack_from('<I', reference, 463)[0] == checksum(original):
        struct.pack_into('<I', result, 463, checksum(plain))
    result = bytes(result)
    if unpack(result)[1] != plain:
        raise ValueError('Scenario encoding did not round trip')
    return result
