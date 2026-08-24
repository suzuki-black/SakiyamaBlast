"""ゲームのパレット定義。src/main.c の vdp_set_palette() と必ず一致させること。

MSX2 のパレットは R/G/B 各 0-7。絵の変換ツールはここを見て、PNG の色を
一番近いパレット番号へ写す。
"""

PALETTE = {
    1:  (0, 0, 0),   # COL_TUNNEL  掘った坑道 (黒)
    2:  (7, 4, 3),   # COL_SKIN    肌
    3:  (2, 1, 1),   # COL_DARK    影・長靴
    4:  (1, 1, 2),   # COL_HUD     HUD の帯
    5:  (2, 5, 7),   # COL_BATH    湯
    6:  (4, 2, 1),   # COL_DIRT    土 (面ごとに変わる)
    7:  (2, 3, 6),   # COL_BODY    作業着
    8:  (6, 1, 1),   # COL_RED     ダイナマイト
    9:  (7, 3, 2),   # COL_RAT     ネズミ
    10: (7, 6, 2),   # COL_FUSE    導火線・たが
    11: (7, 6, 1),   # COL_BLAST   爆風
    12: (3, 6, 7),   # COL_DIA     ダイヤモンド
    13: (7, 2, 7),   # COL_BAT     コウモリ
    14: (5, 5, 5),   # COL_ROCK    岩
    15: (7, 7, 7),   # COL_WHITE   白
}

# ASCII アートで使う記号 -> パレット番号
LEGEND = {
    '.': 0,   'K': 3,   'S': 2,   'B': 7,   'W': 15,  'Y': 10,
    'R': 8,   'G': 14,  'C': 12,  'P': 13,  'O': 9,   'U': 5,
    'D': 6,   'L': 1,
}


def rgb255(idx):
    r, g, b = PALETTE[idx]
    return (round(r * 255 / 7), round(g * 255 / 7), round(b * 255 / 7))


def nearest(rgb):
    """255段階の RGB を一番近いパレット番号へ"""
    best, bi = 1 << 30, 1
    for i in PALETTE:
        pr, pg, pb = rgb255(i)
        d = (pr - rgb[0]) ** 2 + (pg - rgb[1]) ** 2 + (pb - rgb[2]) ** 2
        if d < best:
            best, bi = d, i
    return bi
