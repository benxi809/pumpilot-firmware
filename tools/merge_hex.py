#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""合并两个 Intel HEX 文件（无重叠地址区）为单一可烧录 hex。
用法: merge_hex.py softdevice.hex app.hex out.hex
修正版：所有记录的校验和均正确计算。"""
import sys

def checksum(body):
    """Intel HEX 校验和 = (~sum + 1) & 0xFF，sum 为所有字节(含长度/地址/类型/数据)。"""
    s = 0
    for i in range(0, len(body), 2):
        s += int(body[i:i+2], 16)
    return ((~s) + 1) & 0xFF

def parse_hex(path):
    data = {}
    base = 0
    for line in open(path):
        line = line.strip()
        if not line or line[0] != ':':
            continue
        bc = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rectype = int(line[7:9], 16)
        payload = bytes.fromhex(line[9:9+2*bc])
        if rectype == 0x04:
            base = (int.from_bytes(payload, 'big') << 16)
        elif rectype == 0x02:
            base = (int.from_bytes(payload, 'big') << 4)
        elif rectype == 0x00:
            data[base + addr] = payload
    return data

def emit(data):
    out = []
    last_hi = None
    for addr in sorted(data):
        b = data[addr]
        for off in range(0, len(b), 16):
            chunk = b[off:off+16]
            a = addr + off
            hi = (a >> 16) & 0xFF
            if hi != last_hi:
                # Extended Linear Address 记录: 类型04, 数据2字节(高16位)
                # 格式: :02 0000 04 HIHI CC
                body = "02000004" + f"{hi:02X}00"
                out.append(":" + body + f"{checksum(body):02X}")
                last_hi = hi
            lo = a & 0xFFFF
            body = f"{len(chunk):02X}{lo:04X}00" + chunk.hex().upper()
            out.append(":" + body + f"{checksum(body):02X}")
    out.append(":00000001FF")
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__); sys.exit(1)
    a = parse_hex(sys.argv[1])
    b = parse_hex(sys.argv[2])
    overlap = set(a) & set(b)
    if overlap:
        print("WARN overlap:", sorted(overlap)[:5], "app 优先")
    merged = dict(a); merged.update(b)
    with open(sys.argv[3], "w") as f:
        f.write(emit(merged))
    total = sum(len(v) for v in merged.values())
    print(f"merged {sys.argv[3]}: {len(merged)} records, {total} bytes total")
