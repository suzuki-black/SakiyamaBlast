#!/usr/bin/env python3
"""SDCC の .ihx を MSX MegaROM (ASCII8) イメージへ変換する。

ROM 配置:
    seg0 -> 0x4000-0x5FFF  固定 (ROM ヘッダ + コード)
    seg1 -> 0x6000-0x7FFF  固定 (コード)
    seg2 -> 0x8000-0x9FFF  固定 (コード)
    seg3 -> 0xA000-0xBFFF  可変データ窓 (bank_select で差し替え)

固定コードは 0x4000-0x9FFF (24KB) に収まらなければならない。はみ出したら
バンク切り替えなしでは届かないので、エラーにして気づけるようにする。
"""
import sys

ROM_BASE = 0x4000
FIXED_END = 0xA000        # ここより後ろは可変窓なので固定コードは置けない
WINDOW_END = 0xC000
SEG_SIZE = 0x2000


def parse_ihx(path):
    mem = {}
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"{path}:{lineno}: IHX 形式ではありません")
            raw = bytes.fromhex(line[1:])
            count, addr_hi, addr_lo, rectype = raw[0], raw[1], raw[2], raw[3]
            if (sum(raw) & 0xFF) != 0:
                raise ValueError(f"{path}:{lineno}: チェックサム不一致")
            if rectype == 0x01:
                break
            if rectype != 0x00:
                raise ValueError(f"{path}:{lineno}: 未対応のレコード種別 {rectype:02X}")
            addr = (addr_hi << 8) | addr_lo
            for i in range(count):
                mem[addr + i] = raw[4 + i]
    return mem


def main(argv):
    if len(argv) < 3:
        print("usage: ihx2rom.py <in.ihx> <out.rom> [size_kb] [seg:file ...]",
              file=sys.stderr)
        return 2
    ihx, out = argv[1], argv[2]
    size_kb = int(argv[3]) if len(argv) > 3 and ":" not in argv[3] else 64
    blobs = [a for a in argv[3:] if ":" in a]

    mem = parse_ihx(ihx)
    if not mem:
        print("error: IHX にデータがありません", file=sys.stderr)
        return 1

    lo, hi = min(mem), max(mem)
    if lo < ROM_BASE:
        print(f"error: 0x{lo:04X} は ROM 領域 (0x4000-) の外です", file=sys.stderr)
        return 1
    if hi >= WINDOW_END:
        print(f"error: 0x{hi:04X} は ROM 領域 (-0xBFFF) を超えています", file=sys.stderr)
        return 1
    if hi >= FIXED_END:
        print(f"error: コード末尾 0x{hi:04X} が固定領域 0x4000-0x9FFF を超えました。"
              f" データをバンク側へ移すか分割してください。", file=sys.stderr)
        return 1

    total = size_kb * 1024
    if total % SEG_SIZE:
        print("error: ROM サイズは 8KB の倍数で指定してください", file=sys.stderr)
        return 1

    rom = bytearray(b"\xFF" * total)
    for addr, val in mem.items():
        rom[addr - ROM_BASE] = val

    # 可変バンク側へ載せるデータ (タイトル画像など)。seg:file の形で指定する。
    for spec in blobs:
        seg, path = spec.split(":", 1)
        seg = int(seg)
        with open(path, "rb") as f:
            data = f.read()
        off = seg * SEG_SIZE
        if off + len(data) > total:
            print("error: %s (%d バイト) がセグメント %d から ROM に収まりません"
                  % (path, len(data), seg), file=sys.stderr)
            return 1
        if seg < 4:
            print("error: セグメント %d は固定コード領域です" % seg, file=sys.stderr)
            return 1
        rom[off:off + len(data)] = data
        print("  seg%d..%d: %s (%d バイト)"
              % (seg, seg + (len(data) - 1) // SEG_SIZE, path, len(data)))

    with open(out, "wb") as f:
        f.write(rom)

    used = hi - ROM_BASE + 1
    print(f"{out}: {total // 1024}KB ({total // SEG_SIZE} segments) / "
          f"code+data 0x{lo:04X}-0x{hi:04X} = {used} bytes, "
          f"fixed area free {FIXED_END - hi - 1} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
