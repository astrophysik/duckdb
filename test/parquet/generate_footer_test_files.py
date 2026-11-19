#!/usr/bin/env python3
# Writes parquet files whose footers exercise every path of the thrift compact protocol reader.
# usage: footer_test_files.py <a parquet file> <output directory>
import struct
import sys
from pathlib import Path

(
    CT_BOOLEAN_TRUE,
    CT_BOOLEAN_FALSE,
    CT_BYTE,
    CT_I16,
    CT_I32,
    CT_I64,
    CT_DOUBLE,
    CT_BINARY,
    CT_LIST,
    CT_SET,
    CT_MAP,
    CT_STRUCT,
) = range(1, 13)


def varint(value):
    out = bytearray()
    while value >= 0x80:
        out.append(value & 0x7F | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def zigzag(value):
    return (value << 1) ^ (value >> 63)


def field(field_id, compact_type):
    # the long form of a field header: the type, then the field id
    return bytes([compact_type]) + varint(zigzag(field_id))


def short_field(delta, compact_type):
    # the short form: the delta to the previous field id and the type in one byte
    return bytes([delta << 4 | compact_type])


def binary(data):
    return varint(len(data)) + data


def list_header(size, element_type):
    if size < 15:
        return bytes([size << 4 | element_type])
    return bytes([0xF0 | element_type]) + varint(size)


def double(value):
    return struct.pack('<d', value)


STOP = b'\x00'

UNKNOWN_FIELDS = b''.join(
    [
        field(1000, CT_BOOLEAN_TRUE),
        field(1001, CT_BOOLEAN_FALSE),
        field(1002, CT_BYTE) + struct.pack('<b', -5),
        field(1003, CT_I16) + varint(zigzag(-300)),
        field(1004, CT_I32) + varint(zigzag(123456)),
        field(1005, CT_I64) + varint(zigzag(-(1 << 40))),
        field(1006, CT_DOUBLE) + double(3.5),
        field(1007, CT_BINARY) + binary(b'unknown'),
        field(1008, CT_LIST)
        + list_header(20, CT_BOOLEAN_TRUE)
        + bytes(CT_BOOLEAN_TRUE if i % 3 else CT_BOOLEAN_FALSE for i in range(20)),
        field(1009, CT_SET) + list_header(3, CT_I16) + b''.join(varint(zigzag(v)) for v in (1, -2, 3)),
        field(1010, CT_MAP)
        + varint(2)
        + bytes([CT_BINARY << 4 | CT_LIST])
        + b''.join(
            binary(key) + list_header(2, CT_DOUBLE) + double(a) + double(b)
            for key, a, b in ((b'a', 1.0, 2.0), (b'b', -1.0, 0.25))
        ),
        field(1011, CT_MAP) + varint(0),
        field(1012, CT_STRUCT)
        + short_field(1, CT_I32)
        + varint(zigzag(7))
        + short_field(2, CT_STRUCT)
        + short_field(1, CT_BINARY)
        + binary(b'nested')
        + short_field(3, CT_BOOLEAN_TRUE)
        + STOP
        + short_field(1, CT_I64)
        + varint(zigzag(-1))
        + STOP,
    ]
)

BROKEN = {
    # the stop of the metadata is missing - the reader runs out of bytes
    'footer_truncated.parquet': b'',
    'footer_varint_too_long.parquet': field(1000, CT_I64) + b'\xff' * 10 + b'\x01' + STOP,
    'footer_negative_string_size.parquet': field(1000, CT_BINARY) + varint(0xFFFFFFFF) + STOP,
    'footer_negative_list_size.parquet': field(1000, CT_LIST) + list_header(15, CT_I32)[:1] + varint(0xFFFFFFFF) + STOP,
    'footer_list_too_long.parquet': field(1000, CT_LIST) + list_header(1000000, CT_I32) + STOP,
    'footer_unknown_type.parquet': bytes([0x0D]) + varint(zigzag(1000)) + STOP,
}


def write(path, body, footer):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(body + footer + struct.pack('<I', len(footer)) + b'PAR1')


def main():
    source, out = Path(sys.argv[1]), Path(sys.argv[2])
    data = source.read_bytes()
    if data[-4:] != b'PAR1':
        raise SystemExit(f'{source} is not a parquet file')
    footer_len = struct.unpack('<I', data[-8:-4])[0]
    body, footer = data[: -8 - footer_len], data[-8 - footer_len : -8]
    if footer[-1:] != STOP:
        raise SystemExit(f'the footer of {source} does not end with the stop of its struct')
    metadata = footer[:-1]
    write(out / 'footer_unknown_fields.parquet', body, metadata + UNKNOWN_FIELDS + STOP)
    for name, tail in BROKEN.items():
        write(out / 'broken' / name, body, metadata + tail)


if __name__ == '__main__':
    main()
