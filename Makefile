# SakiyamaBlast - MSX turbo R / SDCC ビルド
#
#   make          ... build/sakiyama.rom を作る
#   make run      ... openMSX で起動 (MACHINE= で機種変更)
#   make clean

NAME      := sakiyama
SRCDIR    := src
BUILDDIR  := build

# turbo R が実ターゲットだが、コードは z80 で吐いて MSX2/2+ 実機でも動くようにする
# (SCREEN 5 を使うので MSX2 以降は必須)。turbo R では R800 でそのまま速く走る。
CC        := sdcc
AS        := sdasz80
CFLAGS    := -mz80 --std-c99 --sdcccall 1 --opt-code-speed -I$(SRCDIR) -I$(BUILDDIR)
LDFLAGS   := -mz80 --no-std-crt0 --code-loc 0x4010 --data-loc 0xC000 -Wl-b_BANKDATA=0xA000

# 固定領域 (0x4000-0x9FFF) は 24KB しかない。絵と曲は**ただのデータ**なので、
# 0xA000 の窓へリンクして ROM のセグメント3へ置き、固定領域を空ける。
# 実行時は bank_select(DATA_SEG) でこの窓を選んでおく。
# バンク側 (0xA000 の窓) へ置くモジュール。
# **title_draw() の中から呼ばれるものは置けない。** あそこは窓をタイトル画像へ
# 切り替えている最中なので、バンク側のコードは見えなくなる (vdp / lowlevel / msx は固定)。
# それ以外は常に DATA_SEG が選ばれているので、コードを置いても同じ速度で動く。
BANKED_SRC := $(SRCDIR)/art.c $(SRCDIR)/music.c \
              $(SRCDIR)/hud.c $(SRCDIR)/config.c $(SRCDIR)/bonus.c

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
TITLE_OPTS := --black 1275,0,1536,82 --crop 88,0,1498,1024

# スプライトと背景タイルの絵。PNG が正で、描き替えて make し直せば差し替わる。
ART_SPR_SRC  := assets/sprites.png
ART_TILE_SRC := assets/tiles.png
ART_SPR      := $(BUILDDIR)/sprites.h
ART_TILE     := $(BUILDDIR)/tiles.h
PAL_H        := $(BUILDDIR)/palette.h
ART_SPR_NAMES  := miner,bomb,rock,rat,bat,split,stun1,stun2,stun3,brace
ART_TILE_NAMES := powder,bath,bscore,bcool,blife,cave,dia,dyna,diacrk,pillar

# 開発用の既定機種。turbo R の実 BIOS ROM を持っていれば MACHINE=Panasonic_FS-A1GT
MACHINE   ?= C-BIOS_MSX2+_JP

.PHONY: hostsim grade all run check gamedata gamedata-import clean

all: $(ROM)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.s $(MAKEFILE) | $(BUILDDIR)
	$(AS) -g -o $@ $<

$(TITLE_BIN) $(TITLE_PAL): $(TITLE_SRC) tools/png2sc5.py | $(BUILDDIR)
	python3 tools/png2sc5.py $(TITLE_SRC) $(BUILDDIR)/title $(TITLE_OPTS)

$(PAL_H): tools/gen_palette_h.py tools/palette.py | $(BUILDDIR)
	python3 tools/gen_palette_h.py $@

$(ART_SPR): $(ART_SPR_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_SPR_SRC) $@ --mode sprite --names $(ART_SPR_NAMES)

$(ART_TILE): $(ART_TILE_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_TILE_SRC) $@ --mode tile --names $(ART_TILE_NAMES)

# ヘッダにも依存させる。これが無いと game.h の定数を変えても再ビルドされず、
# 古い ROM のまま「直したつもり」になる。
$(BUILDDIR)/%.rel: $(SRCDIR)/%.c $(HEADERS) $(MAKEFILE) $(TITLE_PAL) $(ART_SPR) $(ART_TILE) $(PAL_H) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(if $(filter $<,$(BANKED_SRC)),--codeseg BANKDATA --constseg BANKDATA) -c -o $@ $<

# crt0 が必ず先頭に来るようにリンク順を固定する
$(IHX): $(BUILDDIR)/crt0.rel $(AOBJ) $(COBJ)
	$(CC) $(LDFLAGS) -o $@ $(BUILDDIR)/crt0.rel $(filter-out $(BUILDDIR)/crt0.rel,$(AOBJ)) $(COBJ)

$(ROM): $(IHX) $(TITLE_BIN) tools/ihx2rom.py
	python3 tools/ihx2rom.py $(IHX) $(ROM) 64 $(TITLE_SEG):$(TITLE_BIN)

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
HOSTSIM_SRC := $(SRCDIR)/field.c $(SRCDIR)/enemy.c $(SRCDIR)/player.c \
               $(SRCDIR)/rock.c $(SRCDIR)/bomb.c $(SRCDIR)/bonus.c \
               tools/hostsim/stubs.c tools/hostsim/sim.c

tools/hostsim/sim: $(HOSTSIM_SRC) $(HEADERS)
	cc -std=c99 -O2 -Wall -DHOSTSIM -D__z88dk_fastcall= -I$(SRCDIR) -o $@ $(HOSTSIM_SRC)

hostsim: tools/hostsim/sim

# 面1〜15を採点する。合格条件を外れた面は配置を振り直す判断材料になる。
grade: tools/hostsim/sim
	python3 tools/hostsim/metrics.py --all --runs 24

check: $(ROM)
	sh tools/smoketest.sh $(ROM)
