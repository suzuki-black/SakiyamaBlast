# SakiyamaBlast - MSX turbo R / SDCC ビルド
#
#   make          ... build/sakiyama.rom を作る
#   make run      ... openMSX で起動 (MACHINE= で機種変更)
#   make clean

NAME      := sakiyama
SRCDIR    := src
BUILDDIR  := build

# turbo R が実ターゲットだが、コードは z80 で吐いて MSX2/2+ 実機でも動くようにする
# (SCREEN 5 を使うので MSX2 以降は必須)。
# **「turbo R なら R800 で勝手に速くなる」は誤り。**
#   - **カートリッジは必ず Z80 モードで起動する。** 自分で CHGCPU (0x0180) を
#     呼ばない限り R800 にはならない。このゲームは呼んでいないので、
#     実機の turbo R でも Z80 3.58MHz で動いている
#   - 仮に R800 へ切り替えても、**カートリッジスロットのクロックは常に 3.58MHz**で、
#     S1990 が外部スロットと I/O アクセスに wait を入れる。**VDP は I/O なので、
#     このゲームの律速 (VRAM 書き込み) は速くならない**
#   - 速くなるのは**内蔵 DRAM にあるもの**だけ。R800 DRAM モードでは内蔵 BIOS ROM が
#     DRAM へ写るので BIOS 呼び出しと割り込み処理が速くなり、RAMCODE (0xD000) も速くなる
#   - ただし **R800 は OUT が連続すると VDP が追いつかない**。切り替えるなら
#     vdp_wblock の OTIR など全ての VDP アクセスを見直す必要がある
CC        := sdcc
AS        := sdasz80
# **最適化は置き場ごとに変える。** 固定領域と RAM 実行は速度優先、**窓側は大きさ優先**。
# 窓 (8KB) が一番きつく、毎フレーム走るものが少ない (config/levels/ending は冷たい)。
# 速度を測って問題が出たら、そのモジュールだけ speed へ戻すこと。
CFLAGS    := -mz80 --std-c99 --sdcccall 1 -I$(SRCDIR) -I$(BUILDDIR)
OPT_SPEED := --opt-code-speed
OPT_SIZE  := --opt-code-size
# **field.c も大きさ優先。** 9KB あるが、その大半は面の組み立て (1面に1回) で、
# 毎フレーム走るのは描画の数本だけ。速度への影響は実測して確かめること。
SIZE_SRC   = $(BANKED_SRC) $(SRCDIR)/field.c    # **遅延展開。** BANKED_SRC はこの後で定義される
LDFLAGS   := -mz80 --no-std-crt0 --code-loc 0x4010 --data-loc 0xC000 -Wl-b_BANKDATA=0xA000 -Wl-b_MUSICDATA=0x2000 -Wl-b_RAMCODE=0xC9A0

# 固定領域 (0x4000-0x9FFF) は 24KB しかない。絵と曲は**ただのデータ**なので、
# 0xA000 の窓へリンクして ROM のセグメント3へ置き、固定領域を空ける。
# 実行時は bank_select(DATA_SEG) でこの窓を選んでおく。
# バンク側 (0xA000 の窓) へ置くモジュール。
# **title_draw() の中から呼ばれるものは置けない。** あそこは窓をタイトル画像へ
# 切り替えている最中なので、バンク側のコードは見えなくなる (vdp / lowlevel / msx は固定)。
# それ以外は常に DATA_SEG が選ばれているので、コードを置いても同じ速度で動く。
# **bonus.c は固定領域へ戻した** (2026-10-26)。裏面のガスを水 (窓側) へ足したら
# 窓が 70バイト溢れたため。固定領域・窓・RAM実行の3つは、どれも数百バイトしか
# 空いていないので、**足すときはどこから削るかを同時に決める**こと。
# **art と sprite は窓に置けない。** 絵のセグメントへ窓を向けている間に
# 自分が見えなくなるため (art.c は番地を返すだけ、sprite.c は窓を切り替える側)。
BANKED_SRC := \
              $(SRCDIR)/config.c $(SRCDIR)/debris.c $(SRCDIR)/kanji.c \
              $(SRCDIR)/haze.c $(SRCDIR)/water.c $(SRCDIR)/hud.c $(SRCDIR)/cart.c \
              $(SRCDIR)/levels.c $(SRCDIR)/ending.c

# **RAM へ写して実行するモジュール。**
# 0x4000-0xBFFF の 32KB はもう埋まっていて、並べ替えでは空きが作れない。
# RAM (ページ3) は変数が 0xC000 から約3.2KB、スタックが 0xF380 から 0xF30F までしか
# 伸びない (openMSX で実測) ので、0xCA80-0xF1FF が空いている
# (2026-10-22 に 0xCD00 から下げた。升ごとの真偽値を cflag[] へ詰めて
#  変数が 0xC9E5 までに縮んだぶん、RAM 実行の枠を 640バイト広げた)。
# **8KB を超えたので ROM のセグメントは2枚**。写す手順も2回に分かれる (main.c)。
# **vdp はここに置く。** RAM はバンク窓の切り替えの影響を受けないので、
# タイトル画像を読んでいる最中 (title_draw) に呼ばれても見えている。
# **title.c は固定領域へ戻した** (2026-10-25)。固定領域も窓の切り替えでは消えないので
# 条件を満たす。RAM 実行の空きが6バイトになり、裏面のガス判定が入らなくなったため。
# 起動時に main() が ROM のセグメント8 から写す。
# **写す前に呼ばれるものは置けない** (crt0 / lowlevel / main の冒頭)。
RAM_SRC   := $(SRCDIR)/enemy.c $(SRCDIR)/bomb.c $(SRCDIR)/sound.c $(SRCDIR)/opll.c $(SRCDIR)/pcm.c \
             $(SRCDIR)/vdp.c $(SRCDIR)/text.c $(SRCDIR)/tlimit.c

CSRC      := $(wildcard $(SRCDIR)/*.c)
HEADERS   := $(wildcard $(SRCDIR)/*.h)
# Makefile 自身も依存に入れる。コンパイル指定やリンク位置を変えたのに
# 作り直されず、古い ROM で「直したつもり」になるのを防ぐ。
MAKEFILE  := $(lastword $(MAKEFILE_LIST))
ASRC      := $(SRCDIR)/crt0.s $(SRCDIR)/lowlevel.s $(SRCDIR)/bios.s
COBJ      := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.rel,$(CSRC))
AOBJ      := $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.rel,$(ASRC))

ROM       := $(BUILDDIR)/$(NAME).rom
IHX       := $(BUILDDIR)/$(NAME).ihx

# タイトル1枚絵。assets/title.png -> SC5 のピクセル(27,136B) ＋ パレット(C ヘッダ)
TITLE_SRC := assets/title.png
TITLE_BIN := $(BUILDDIR)/title.bin
TITLE_PAL := $(BUILDDIR)/title_pal.h
TITLE_SEG := 4
# 元絵の透かしを黒で消し、4:3 になるよう左右を落としてから 256x212 へ縮める
# **副題は絵から消して、ゲーム側の字で描き直す** (2026-10-25)。生成画像の漢字が
# 崩れていて「先」が「光」、「野」が「墅」に見えるため。2つめの --black がその範囲。
TITLE_OPTS := --black 1275,0,1536,82 --black 330,222,1062,338 --crop 88,0,1498,1024

# スプライトと背景タイルの絵。PNG が正で、描き替えて make し直せば差し替わる。
ART_SPR_SRC  := assets/sprites.png
ART_TILE_SRC := assets/tiles.png
ART_SPR      := $(BUILDDIR)/sprites.h
ART_TILE     := $(BUILDDIR)/tiles.h
ART_SPR_BIN  := $(BUILDDIR)/sprites.bin
ART_TILE_BIN := $(BUILDDIR)/tiles.bin
ART_TILE_SEG := 10
ART_SPR_SEG  := 11
PAL_H        := $(BUILDDIR)/palette.h
# 名前はカンマ区切りで**1行**に書く。行継続で折ると空白が入って別の引数になる。
# **和太鼓の波形** (turbo R の PCM 用)。合成なので素材の権利を持ち込まない。
TAIKO_BIN := $(BUILDDIR)/taiko.bin
TAIKO_H   := $(BUILDDIR)/taiko.h
TAIKO_SEG := 12
# **漢字の字形**。窓から追い出して専用セグメントへ (blob_read で読む)。
KANJI_BIN := $(BUILDDIR)/kanji.bin
KANJI_H   := $(BUILDDIR)/kanji.h
KANJI_SEG := 13
# **曲データ**。窓から追い出して専用セグメントへ (sound.c が読む前に窓を向ける)。
MUSIC_SRC := $(SRCDIR)/music.c $(SRCDIR)/endtext.c
MUSIC_SEG := 14

ART_SPR_NAMES  := miner,bomb,rock,rat,bat,split,stun1,stun2,stun3,brace,wd1,wd2,wu1,wu2,wl1,wl2,wr1,wr2,bru,brl,brr,std,stu,stl,str,chip,cart,cartrock,rockprop
ART_TILE_NAMES := powder,bath,bscore,bcool,blife,cave,dia,dyna,diacrk,pillar,spring,springoff,toir,toil,toibr,toibl,rail,raill,railr,gas

# 開発用の既定機種。turbo R の実 BIOS ROM を持っていれば MACHINE=Panasonic_FS-A1GT
MACHINE   ?= C-BIOS_MSX2+_JP

.PHONY: hostsim grade emotion all run check gamedata gamedata-import clean

all: $(ROM)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.s $(MAKEFILE) | $(BUILDDIR)
	$(AS) -g -o $@ $<

$(TITLE_BIN) $(TITLE_PAL): $(TITLE_SRC) tools/png2sc5.py | $(BUILDDIR)
	python3 tools/png2sc5.py $(TITLE_SRC) $(BUILDDIR)/title $(TITLE_OPTS)

$(PAL_H): tools/gen_palette_h.py tools/palette.py | $(BUILDDIR)
	python3 tools/gen_palette_h.py $@

# 絵は C の配列ではなく**バイナリ**にして、ROM の専用セグメントへ入れる
# (0xA000 の窓は 8KB しかないので、絵で埋めるとコードが置けなくなる)。
# ヘッダ側には枚数と名前の define だけが入る。
$(ART_SPR) $(ART_SPR_BIN): $(ART_SPR_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_SPR_SRC) $(ART_SPR) --mode sprite \
	    --names $(ART_SPR_NAMES) --bin $(ART_SPR_BIN)

$(ART_TILE) $(ART_TILE_BIN): $(ART_TILE_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_TILE_SRC) $(ART_TILE) --mode tile \
	    --names $(ART_TILE_NAMES) --bin $(ART_TILE_BIN)

# ヘッダにも依存させる。これが無いと game.h の定数を変えても再ビルドされず、
# 古い ROM のまま「直したつもり」になる。
$(TAIKO_BIN) $(TAIKO_H): tools/gen_taiko.py | $(BUILDDIR)
	python3 tools/gen_taiko.py $(TAIKO_BIN) $(TAIKO_H)

$(KANJI_BIN) $(KANJI_H): tools/mkkanji.py | $(BUILDDIR)
	python3 tools/mkkanji.py --bin $(KANJI_BIN) $(KANJI_H)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.c $(HEADERS) $(MAKEFILE) $(TITLE_PAL) $(ART_SPR) $(ART_TILE) $(PAL_H) $(TAIKO_H) $(KANJI_H) | $(BUILDDIR)
	$(CC) $(CFLAGS) \
	  $(if $(filter $<,$(SIZE_SRC)),$(OPT_SIZE),$(OPT_SPEED)) \
	  $(if $(filter $<,$(MUSIC_SRC)),--codeseg MUSICDATA --constseg MUSICDATA) \
	  $(if $(filter $<,$(BANKED_SRC)),--codeseg BANKDATA --constseg BANKDATA) \
	  $(if $(filter $<,$(RAM_SRC)),--codeseg RAMCODE --constseg RAMCODE) -c -o $@ $<

# crt0 が必ず先頭に来るようにリンク順を固定する
$(IHX): $(BUILDDIR)/crt0.rel $(AOBJ) $(COBJ)
	$(CC) $(LDFLAGS) -o $@ $(BUILDDIR)/crt0.rel $(filter-out $(BUILDDIR)/crt0.rel,$(AOBJ)) $(COBJ)

$(ROM): $(IHX) $(TITLE_BIN) $(ART_TILE_BIN) $(ART_SPR_BIN) $(TAIKO_BIN) $(KANJI_BIN) tools/ihx2rom.py
	python3 tools/ihx2rom.py $(IHX) $(ROM) 128 $(TITLE_SEG):$(TITLE_BIN) \
	    $(ART_TILE_SEG):$(ART_TILE_BIN) $(ART_SPR_SEG):$(ART_SPR_BIN) \
	    $(TAIKO_SEG):$(TAIKO_BIN) $(KANJI_SEG):$(KANJI_BIN)

run: $(ROM)
	openmsx -machine $(MACHINE) -carta $(ROM) -romtype ASCII8

# パターンエディタ等へ渡す、ゲーム1本ぶんのグラフィックデータ (仕様は docs/GAMEDATA_FORMAT.md)
# 名前は import 側と同じく Makefile から渡す。ツールが自前で名前を持つと、
# 絵を足したときに黙って古いままになり、往復が壊れる (実際に壊れていた)。
gamedata: $(ART_SPR_SRC) $(ART_TILE_SRC) tools/export_gamedata.py tools/palette.py
	python3 tools/export_gamedata.py gamedata.json \
	    --sprite-names $(ART_SPR_NAMES) --tile-names $(ART_TILE_NAMES)

# パターンエディタで編集した gamedata.json を取り込む (assets の PNG を書き戻す)
gamedata-import: gamedata.json tools/import_gamedata.py tools/palette.py
	python3 tools/import_gamedata.py gamedata.json \
	    --sprites $(ART_SPR_SRC) --sprite-names $(ART_SPR_NAMES) \
	    --tiles $(ART_TILE_SRC) --tile-names $(ART_TILE_NAMES)

clean:
	rm -rf $(BUILDDIR)

# ---- 面の数値化 (ホスト側。MSX の ROM とは別物) ----
# src/ のロジックをそのまま gcc でビルドし、VDP/PSG だけスタブへ差し替えて
# ヘッドレスで走らせる。詳細は docs/HOSTSIM.md。
HOSTSIM_SRC := $(SRCDIR)/field.c $(SRCDIR)/enemy.c $(SRCDIR)/player.c $(SRCDIR)/water.c \
               $(SRCDIR)/rock.c $(SRCDIR)/bomb.c $(SRCDIR)/bonus.c $(SRCDIR)/cart.c \
               $(SRCDIR)/levels.c \
               tools/hostsim/stubs.c tools/hostsim/sim.c

tools/hostsim/sim: $(HOSTSIM_SRC) $(HEADERS)
	cc -std=c99 -O2 -Wall -DHOSTSIM -D__z88dk_fastcall= -I$(SRCDIR) -o $@ $(HOSTSIM_SRC)

hostsim: tools/hostsim/sim

# 面1〜15を採点する。合格条件を外れた面は配置を振り直す判断材料になる。
grade: tools/hostsim/sim
	python3 tools/hostsim/metrics.py --all --runs 24

# 面ごとの「感情」を数にする (ストレスと爽快感)。詳細は tools/hostsim/emotion.py の説明。
emotion: tools/hostsim/sim
	python3 tools/hostsim/emotion.py --all --runs 16

check: $(ROM)
	sh tools/smoketest.sh $(ROM)
