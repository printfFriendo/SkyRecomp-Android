#!/usr/bin/env python3
"""
ROM Extractor for The Legend of Zelda: Skyward Sword HD (Switch -> PC/Android Recomp)
Extracts ExeFS (code) and RomFS (data) from user-provided legally obtained NSP.
"""

import os
import sys
import time
from Crypto.Cipher import AES

DEFAULT_KEY = bytes.fromhex("fd88a75ad7b4d195d919ee496c76278b")

def load_keys(keys_path=None):
    candidates = [
        keys_path,
        r"C:\Users\Dio\Desktop\Castlevania\prod.keys",
        "prod.keys",
        "keys.txt",
        os.path.expanduser("~/.switch/prod.keys")
    ]
    for p in candidates:
        if p and os.path.exists(p):
            try:
                kaeks = {}
                with open(p, "r", encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        line = line.strip()
                        if "key_area_key_application_" in line and "source" not in line and "=" in line:
                            k, v = [x.strip() for x in line.split("=", 1)]
                            idx = int(k.split("_")[-1], 16)
                            kaeks[idx] = bytes.fromhex(v)
                if 0x0a in kaeks:
                    enc_k2 = bytes.fromhex("b22991688d17b28f37e1258e3ce271ae")
                    dec_key = AES.new(kaeks[0x0a], AES.MODE_ECB).decrypt(enc_k2)
                    print(f"[KEYS] Successfully derived game key from {p}")
                    return dec_key
            except Exception as e:
                print(f"[WARN] Error reading {p}: {e}")
    return DEFAULT_KEY

def extract(nsp_path, keys_path=None, out_dir="extracted"):
    print("=" * 60)
    print("  Skyward Sword HD Rom Extractor & Setup")
    print("=" * 60)
    if not nsp_path or not os.path.exists(nsp_path):
        print(f"[ERROR] ROM file not found: {nsp_path}", file=sys.stderr)
        return False

    os.makedirs(out_dir, exist_ok=True)
    exefs_dir = os.path.join(out_dir, "exefs")
    os.makedirs(exefs_dir, exist_ok=True)
    romfs_path = os.path.join(out_dir, "romfs.bin")

    print(f"Reading ROM: {nsp_path}")
    print(f"Destination: {out_dir}")

    game_key = load_keys(keys_path)

    # 1. Parse NSP PFS0 header to locate Program NCA offset
    with open(nsp_path, "rb") as f_in:
        magic = f_in.read(4)
        if magic != b"PFS0":
            print(f"[ERROR] Invalid NSP magic: {magic}", file=sys.stderr)
            return False
        num_files = int.from_bytes(f_in.read(4), "little")
        str_sz = int.from_bytes(f_in.read(4), "little")
        f_in.read(4) # reserved
        nca_offset = 16 + num_files * 24 + str_sz
        print(f"[NSP] PFS0 contains {num_files} files, Program NCA offset: 0x{nca_offset:x}")

        # Check if ExeFS needs extraction
        main_path = os.path.join(exefs_dir, "main")
        if not os.path.exists(main_path):
            print("\nDecrypting and extracting ExeFS (code binaries)...")
            t_exe = time.time()
            sec0_offset = nca_offset + 0x1c3088000
            total_exefs_size = 0x1914000  # ~25 MB
            f_in.seek(sec0_offset)
            enc_exefs = f_in.read(total_exefs_size)

            iv_exefs = bytes.fromhex("0000000100000000000000001c308800")
            dec_exefs = AES.new(game_key, AES.MODE_CTR, initial_value=iv_exefs, nonce=b"").decrypt(enc_exefs)

            pfs0 = dec_exefs[0x8000:]
            if pfs0[:4] == b"PFS0":
                p_num = int.from_bytes(pfs0[4:8], "little")
                p_str_sz = int.from_bytes(pfs0[8:12], "little")
                p_entries = []
                pos = 16
                for _ in range(p_num):
                    off = int.from_bytes(pfs0[pos : pos + 8], "little")
                    sz = int.from_bytes(pfs0[pos + 8 : pos + 16], "little")
                    noff = int.from_bytes(pfs0[pos + 16 : pos + 20], "little")
                    pos += 24
                    p_entries.append((off, sz, noff))
                names = pfs0[pos : pos + p_str_sz]
                data_start = pos + p_str_sz
                for off, sz, noff in p_entries:
                    fname = names[noff:].split(b"\x00", 1)[0].decode("utf-8")
                    fdata = pfs0[data_start + off : data_start + off + sz]
                    out_path = os.path.join(exefs_dir, fname)
                    with open(out_path, "wb") as f_out:
                        f_out.write(fdata)
                    print(f"  [+] {fname} ({len(fdata)/(1024*1024):.2f} MB)")
                print(f"[OK] ExeFS extraction finished in {time.time() - t_exe:.2f}s -> {exefs_dir}")
            else:
                print("[ERROR] Could not find PFS0 magic in decrypted ExeFS section.", file=sys.stderr)
                return False
        else:
            print(f"[OK] ExeFS binaries already extracted in '{exefs_dir}'.")

        # 2. Check if RomFS needs extraction
        if not os.path.exists(romfs_path):
            sec1_start = 0x1c000
            lvl5_rel_off = 0xe28000
            abs_lvl5_off = sec1_start + lvl5_rel_off # 0xe44000
            lvl5_size = 0x1c2242bdc # ~7.55 GB
            file_lvl5_off = nca_offset + abs_lvl5_off

            sec_ctr = bytes.fromhex("0000000002000000")
            c_ctr = bytearray(16)
            for j in range(8):
                c_ctr[j] = sec_ctr[8 - j - 1]
            ofs_val = abs_lvl5_off >> 4
            for j in range(8):
                c_ctr[0x10 - j - 1] = ofs_val & 0xff
                ofs_val >>= 8

            print(f"\nDecrypting and extracting RomFS ({lvl5_size / (1024*1024*1024):.2f} GB)...")
            t0 = time.time()
            chunk_size = 16 * 1024 * 1024  # 16 MB chunks
            bytes_left = lvl5_size
            cur_pos = 0

            cipher = AES.new(game_key, AES.MODE_CTR, initial_value=bytes(c_ctr), nonce=b"")

            with open(romfs_path, "wb") as f_out:
                f_in.seek(file_lvl5_off)
                while bytes_left > 0:
                    to_read = min(bytes_left, chunk_size)
                    enc_chunk = f_in.read(to_read)
                    if not enc_chunk:
                        break
                    dec_chunk = cipher.decrypt(enc_chunk)
                    f_out.write(dec_chunk)
                    bytes_left -= len(enc_chunk)
                    cur_pos += len(enc_chunk)

                    pct = (cur_pos / lvl5_size) * 100
                    elapsed = time.time() - t0
                    speed = (cur_pos / (1024 * 1024)) / (elapsed if elapsed > 0 else 1)
                    print(f"\rProgress: {pct:5.1f}% ({cur_pos / (1024*1024):.0f} / {lvl5_size / (1024*1024):.0f} MB) [{speed:5.1f} MB/s]", end="", flush=True)

            print(f"\n[OK] RomFS extraction finished in {time.time() - t0:.1f}s -> {romfs_path}")
        else:
            print(f"[OK] RomFS data archive already extracted: '{romfs_path}'.")

    return True

if __name__ == "__main__":
    nsp = sys.argv[1] if len(sys.argv) > 1 else r"C:\Projetos\The Legend of Zelda Skyward Sword HD.nsp"
    keys = sys.argv[2] if len(sys.argv) > 2 and not os.path.isdir(sys.argv[2]) else None
    out = sys.argv[3] if len(sys.argv) > 3 else (sys.argv[2] if len(sys.argv) > 2 and os.path.isdir(sys.argv[2]) else "extracted")
    ok = extract(nsp, keys, out)
    sys.exit(0 if ok else 1)
