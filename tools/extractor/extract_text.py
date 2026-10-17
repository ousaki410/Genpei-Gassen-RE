#!/usr/bin/env python3
"""
KOEI LS11 Text Extractor for 《源平合战》 (Genpei Kassen)
Decodes all LS11 streams in Message.gp and parses the string index table.
"""

import os
import json
import struct

def _be32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]

class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.p = 0
        self.nbits = len(data) * 8

    def bit(self) -> int:
        if self.p >= self.nbits:
            raise EOFError("bitstream exhausted")
        b = (self.data[self.p >> 3] >> (7 - (self.p & 7))) & 1
        self.p += 1
        return b

    def value(self) -> int:
        ones = 0
        while self.bit():
            ones += 1
        seg2 = 0
        for _ in range(ones + 1):
            seg2 = (seg2 << 1) | self.bit()
        seg1 = ((1 << ones) - 1) << 1
        return seg1 + seg2

def decompress_ls11_stream(data: bytes, dict_: bytes, dlen: int) -> bytes:
    br = BitReader(data)
    out = bytearray()
    try:
        while len(out) < dlen:
            v = br.value()
            if v < 256:
                out.append(dict_[v])
            else:
                dist = v - 256
                length = br.value() + 3
                for _ in range(length):
                    out.append(out[-dist])
    except EOFError:
        pass
    return bytes(out)

def parse_string_table(entry_bytes: bytes):
    if len(entry_bytes) < 4:
        return []
    first_off = struct.unpack('<H', entry_bytes[:2])[0]
    if first_off <= 0 or first_off >= len(entry_bytes):
        return []
    
    count = first_off // 2
    offsets = []
    for i in range(count):
        off = struct.unpack('<H', entry_bytes[i*2:(i+1)*2])[0]
        offsets.append(off)
    offsets.append(len(entry_bytes))

    results = []
    for i in range(count):
        s_off = offsets[i]
        e_off = offsets[i + 1]
        raw = entry_bytes[s_off:e_off]
        
        # Replace 0x05 with [WAIT], 0x0a with newline
        cleaned = raw.replace(b'\x05', b' [WAIT] ').replace(b'\x0a', b'\n').strip(b'\x00 \r')
        try:
            txt = cleaned.decode('cp932', errors='replace').strip()
        except:
            txt = repr(cleaned)
            
        results.append({
            "id": i,
            "offset": s_off,
            "raw_len": e_off - s_off,
            "text": txt
        })
    return results

def extract_message_gp(src_path: str, out_dir: str):
    with open(src_path, 'rb') as f:
        blob = f.read()

    if not blob.startswith(b"LS11"):
        raise ValueError("Not a valid KOEI LS11 container")

    dict_ = blob[0x10:0x110]
    os.makedirs(out_dir, exist_ok=True)

    entries_meta = []
    o = 0x110
    idx = 0
    while o + 12 <= len(blob):
        clen = _be32(blob, o)
        dlen = _be32(blob, o + 4)
        off  = _be32(blob, o + 8)
        if off == 0 or off >= len(blob) or clen == 0:
            break
        entries_meta.append((idx, clen, dlen, off))
        o += 12
        idx += 1

    print(f"[*] Found {len(entries_meta)} LS11 entries in {src_path}")
    
    all_data = {}
    summary_lines = []

    for idx, clen, dlen, off in entries_meta:
        raw_stream = blob[off:off + clen]
        decomp = decompress_ls11_stream(raw_stream, dict_, dlen)
        print(f"  [+] Entry {idx}: compressed {clen}B -> decompressed {len(decomp)}/{dlen}B")
        
        bin_path = os.path.join(out_dir, f"entry_{idx}.bin")
        with open(bin_path, 'wb') as f_bin:
            f_bin.write(decomp)

        strings = parse_string_table(decomp)
        all_data[f"entry_{idx}"] = strings

        txt_path = os.path.join(out_dir, f"entry_{idx}.txt")
        with open(txt_path, 'w', encoding='utf-8') as f_txt:
            for s in strings:
                f_txt.write(f"[{s['id']:04d}] {s['text']}\n")
                summary_lines.append(f"[{idx}:{s['id']:04d}] {s['text']}")

    json_path = os.path.join(out_dir, "messages.json")
    with open(json_path, 'w', encoding='utf-8') as f_json:
        json.dump(all_data, f_json, ensure_ascii=False, indent=2)

    all_txt_path = os.path.join(out_dir, "messages_all.txt")
    with open(all_txt_path, 'w', encoding='utf-8') as f_all:
        f_all.write("\n".join(summary_lines))

    print(f"[+] All texts exported to {out_dir}")

if __name__ == "__main__":
    extract_message_gp("games/yphz/Message.gp", "extracted_assets/text")
