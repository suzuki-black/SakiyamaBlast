#!/usr/bin/env python3
"""SDCC の .ihx を MSX MegaROM (ASCII8) イメージへ変換する。

ROM 配置:
    seg0 -> 0x4000-0x5FFF  固定 (ROM ヘッダ + コード)
    seg1 -> 0x6000-0x7FFF  固定 (コード)
    seg2 -> 0x8000-0x9FFF  固定 (コード)
    seg3 -> 0xA000-0xBFFF  絵と曲 (0xA000 へリンクしたもの。既定で選んでおく)
    seg4以降 -> 0xA000 の窓へ bank_select() で差し替えて使う (タイトル画像など)

RAM 実行コード:
    0xD000-0xEDFF へリンクされたものは RAMCODE。**ROM の RAMCODE_SEG に置き、
    起動時に main() が RAM へ写して、そこで実行する。** 0x4000-0xBFFF の 32KB は
    もう埋まっており、並べ替えでは空きが作れないため。RAM は 0xC000-0xF37F の
    ページ3で、変数が 0xC000 から約2.5KB、スタックが 0xF380 から下へ約350バイト
    (実測) しか使っていないので、間の 0xD000-0xEDFF が空いている。

固定コードは 0x4000-0x9FFF (24KB) に収まらなければならない。はみ出したら
バンク切り替えなしでは届かないので、エラーにして気づけるようにする。
"""
import os
import re
import sys

ROM_BASE = 0x4000
FIXED_END = 0xA000        # ここより後ろは可変窓なので固定コードは置けない
WINDOW_END = 0xC000
DATA_SEG = 3              # 0xA000 の窓へリンクしたデータを置くセグメント
SEG_SIZE = 0x2000
RAMCODE_BASE = 0xC9A0     # RAM で実行するコードのリンク先 (変数はその手前まで)
RAMCODE_END = 0xF280      # ここより上はスタックのために空ける (実測の最下点 0xF30F)
                          # (実測: スタックは 0xF380 から 0xF30F までしか伸びない。
                          #  タイトル・面の組み立て・3分半の実プレイを通して測った。
                          #  0xF200 との余裕は 271 バイト)
RAMCODE_SEG = 8           # その中身を置く ROM セグメント
# **曲データは自前のセグメントへ。** 窓 (0xA000-0xBFFF) が満杯になったので、
# music.c だけ 0x2000 番地へリンクし、その中身をここへ置く。実行時は窓に見える
# (src/sound.c が読む前に bank_select(MUSIC_SEG) する)。2026-10-23
MUSIC_BASE = 0x2000
MUSIC_END = 0x4000
MUSIC_SEG = 14
# **2枚目の窓 (0x8000-0x9FFF)。** ここへ**複数の領域を同じ番地へ**リンクすると、
# それぞれ別の ROM セグメントへ置かれ、実行時に win2_select() で入れ替えて使える
# (オーバーレイ)。リンカは同じ番地に複数の領域を置けて、.ihx には**それぞれが
# 0x8000 から始まるレコード**として順に出る。そこを切り分けている。
#
# **使っていないときは何も変わらない。** マップに 0x8000 始まりの領域が2つ以上
# 無ければ 0x8000-0x9FFF はただの固定領域のまま (いまはこちら)。
WIN2_BASE = 0x8000
WIN2_END = 0xA000
# オーバーレイを置く ROM セグメント。**1枚目は seg2** (オーバーレイにすると
# 固定領域が 0x4000-0x7FFF = seg0,1 だけになり seg2 が空くため)。
# 増やすときは ROM を大きくして足すこと (いま 128KB = seg0..15)。
WIN2_SEGS = (2, 15)


def parse_ihx(path, overlay=None):
    """IHX を読む。overlay=(lo, hi) を渡すと、その範囲は重複を許して
    **出てきた順の塊 (blob) の列**として返す (オーバーレイの切り分け用)。"""
    mem = {}
    blobs = []
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
            if overlay and overlay[0] <= addr < overlay[1]:
                # **番地が巻き戻ったら次の領域。** 同じ窓へリンクした別の領域が
                # また 0x8000 から始まる、というのがオーバーレイの出方。
                if not blobs or addr < blobs[-1][0] + len(blobs[-1][1]):
                    blobs.append((addr, bytearray()))
                base, buf = blobs[-1]
                buf.extend(raw[4:4 + count])
                continue
            for i in range(count):
                # **同じ番地に2度書かれたら、領域どうしが重なっている。**
                # 固定領域のコードが 0xA000 を越えると、そこへリンクした絵と曲に
                # 重なる。以前は後から書いたほうが黙って勝ち、「free 0 bytes」と
                # 出るだけで壊れた ROM ができていた (2026-09-16)。
                if addr + i in mem:
                    raise ValueError(f"{path}:{lineno}: 0x{addr + i:04X} に2つの領域が"
                                     "重なっています (固定領域 0x4000-0x9FFF が溢れた可能性)")
                mem[addr + i] = raw[4 + i]
    return (mem, blobs) if overlay else mem


def overlay_areas(mapf):
    """リンクマップから「0x8000 へ置かれた領域」を出てきた順に拾う。
    2つ以上あればオーバーレイとして扱う。1つ以下なら普通の固定領域のまま。"""
    out = []
    if not mapf or not os.path.exists(mapf):
        return out
    for line in open(mapf, encoding="utf-8", errors="ignore"):
        m = re.match(r"^_(\w+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s", line)
        if m and int(m.group(2), 16) == WIN2_BASE:
            size = int(m.group(3), 16)
            if size:
                out.append((m.group(1), size))
    return out


def main(argv):
    if len(argv) < 3:
        print("usage: ihx2rom.py <in.ihx> <out.rom> [size_kb] [seg:file ...]",
              file=sys.stderr)
        return 2
    ihx, out = argv[1], argv[2]
    size_kb = int(argv[3]) if len(argv) > 3 and ":" not in argv[3] else 64
    blobs = [a for a in argv[3:] if ":" in a]

    mapf = ihx[:-4] + ".map" if ihx.endswith(".ihx") else None
    ovl = overlay_areas(mapf)
    if len(ovl) < 2:
        ovl = []                      # 1つ以下なら 0x8000 はただの固定領域
    fixed_end = WIN2_BASE if ovl else FIXED_END

    if ovl:
        mem, ovl_blobs = parse_ihx(ihx, overlay=(WIN2_BASE, WIN2_END))
        if len(ovl_blobs) != len(ovl):
            print("error: 0x8000 の領域がマップでは %d 個、IHX では %d 塊です"
                  % (len(ovl), len(ovl_blobs)), file=sys.stderr)
            return 1
        for (name, size), (base, buf) in zip(ovl, ovl_blobs):
            if base != WIN2_BASE or len(buf) != size:
                print("error: 領域 %s はマップで 0x%04X/%d バイトですが、IHX では "
                      "0x%04X/%d バイトです" % (name, WIN2_BASE, size, base, len(buf)),
                      file=sys.stderr)
                return 1
            if size > SEG_SIZE:
                print("error: 領域 %s が %d バイトで 1セグメント (%d) を超えます"
                      % (name, size, SEG_SIZE), file=sys.stderr)
                return 1
        if len(ovl_blobs) > len(WIN2_SEGS):
            print("error: 0x8000 のオーバーレイが %d 枚ありますが、置き場は %d 枚しか"
                  "用意していません (ihx2rom.py の WIN2_SEGS)"
                  % (len(ovl_blobs), len(WIN2_SEGS)), file=sys.stderr)
            return 1
    else:
        mem, ovl_blobs = parse_ihx(ihx), []
    if not mem:
        print("error: IHX にデータがありません", file=sys.stderr)
        return 1

    # **変数が RAM 実行の領域に食い込んでいないか。** 変数 (_DATA / _INITIALIZED) は
    # IHX に載らないのでリンクマップで見る。食い込むと、写したコードを変数が上書きして
    # 何の前触れもなく暴走する。
    if mapf and os.path.exists(mapf):
        end = 0
        for line in open(mapf, encoding="utf-8", errors="ignore"):
            m = re.match(r"^_(DATA|INITIALIZED)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})", line)
            if m:
                end = max(end, int(m.group(2), 16) + int(m.group(3), 16))
        if end > RAMCODE_BASE:
            print(f"error: 変数が 0x{end:04X} まで伸びて RAM 実行の領域 (0x{RAMCODE_BASE:04X}-) "
                  "に食い込んでいます", file=sys.stderr)
            return 1

    # **曲データ (0x2000-0x3FFF) は別扱い。** 窓が満杯なので music.c だけ
    # そこへリンクし、ROM の MUSIC_SEG へ置く (実行時は窓に見える)。
    music = {a: v for a, v in mem.items() if MUSIC_BASE <= a < MUSIC_END}
    for a in music:
        del mem[a]
    lo, hi = min(mem), max(mem)
    if lo < ROM_BASE:
        print(f"error: 0x{lo:04X} は ROM 領域 (0x4000-) の外です", file=sys.stderr)
        return 1
    ramcode = {a: v for a, v in mem.items() if RAMCODE_BASE <= a < RAMCODE_END}
    stray = [a for a in mem if WINDOW_END <= a < RAMCODE_BASE or a >= RAMCODE_END]
    if stray:
        print(f"error: 0x{min(stray):04X} は ROM にも RAMCODE にも置けません",
              file=sys.stderr)
        return 1
    if RAMCODE_END - RAMCODE_BASE > 2 * SEG_SIZE:
        print("error: RAMCODE の領域が 2 セグメント (16384) を超えます", file=sys.stderr)
        return 1
    # 0xA000-0xBFFF は可変窓。ここへリンクされたもの (絵・曲) はセグメント3へ置く。
    fixed_hi = max((a for a in mem if a < fixed_end), default=ROM_BASE - 1)

    total = size_kb * 1024
    if total % SEG_SIZE:
        print("error: ROM サイズは 8KB の倍数で指定してください", file=sys.stderr)
        return 1

    rom = bytearray(b"\xFF" * total)
    for addr, val in mem.items():
        if addr < fixed_end:
            rom[addr - ROM_BASE] = val
        elif addr < WINDOW_END:
            # 窓 0xA000 の内容 = セグメント3 の中身
            rom[DATA_SEG * SEG_SIZE + (addr - FIXED_END)] = val
        else:
            # RAM で走らせるコード。起動時に窓から RAM へ写す。
            # **8KB を超えるぶんは次のセグメントへ続ける** (写す側も2回に分かれる)。
            rom[RAMCODE_SEG * SEG_SIZE + (addr - RAMCODE_BASE)] = val

    # オーバーレイ: 同じ 0x8000 へリンクした領域を、それぞれ別のセグメントへ。
    for i, ((name, _size), (_base, buf)) in enumerate(zip(ovl, ovl_blobs)):
        off = WIN2_SEGS[i] * SEG_SIZE
        rom[off:off + len(buf)] = buf
        print("  seg%d: 窓2 (0x8000) の %s = %d バイト (空き %d)"
              % (WIN2_SEGS[i], name, len(buf), SEG_SIZE - len(buf)))

    for addr, val in music.items():
        rom[MUSIC_SEG * SEG_SIZE + (addr - MUSIC_BASE)] = val

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

    used = fixed_hi - ROM_BASE + 1
    print(f"{out}: {total // 1024}KB ({total // SEG_SIZE} segments) / "
          f"fixed 0x{lo:04X}-0x{fixed_hi:04X} = {used} bytes, "
          f"free {fixed_end - fixed_hi - 1} bytes")
    if ramcode:
        rhi = max(ramcode)
        if music:
            print(f"  seg{MUSIC_SEG}: 曲データ {len(music)} バイト")
        print(f"  seg{RAMCODE_SEG}..{RAMCODE_SEG + (RAMCODE_END - RAMCODE_BASE - 1) // SEG_SIZE}"
              f": RAM実行コード 0x{RAMCODE_BASE:04X}-0x{rhi:04X} = "
              f"{rhi - RAMCODE_BASE + 1} バイト "
              f"(RAM の空き {RAMCODE_END - RAMCODE_BASE} バイト中)")
    banked = [a for a in mem if FIXED_END <= a < WINDOW_END]
    if banked:
        bhi = max(banked)
        print(f"  seg{DATA_SEG}: 絵と曲 0xA000-0x{bhi:04X} = {bhi - FIXED_END + 1} バイト"
              f" (窓 8192 バイト中)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
