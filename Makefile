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
LDFLAGS   := -mz80 --no-std-crt0 --code-loc 0x4010 --data-loc 0xC000

CSRC      := $(wildcard $(SRCDIR)/*.c)
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
ART_SPR_NAMES  := miner,bomb,rock,rat,bat
ART_TILE_NAMES := powder,bath,dia

# 開発用の既定機種。turbo R の実 BIOS ROM を持っていれば MACHINE=Panasonic_FS-A1GT
MACHINE   ?= C-BIOS_MSX2+_JP

.PHONY: all run check clean

all: $(ROM)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.s | $(BUILDDIR)
	$(AS) -g -o $@ $<

$(TITLE_BIN) $(TITLE_PAL): $(TITLE_SRC) tools/png2sc5.py | $(BUILDDIR)
	python3 tools/png2sc5.py $(TITLE_SRC) $(BUILDDIR)/title $(TITLE_OPTS)

$(ART_SPR): $(ART_SPR_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_SPR_SRC) $@ --mode sprite --names $(ART_SPR_NAMES)

$(ART_TILE): $(ART_TILE_SRC) tools/png2art.py tools/palette.py | $(BUILDDIR)
	python3 tools/png2art.py $(ART_TILE_SRC) $@ --mode tile --names $(ART_TILE_NAMES)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.c $(TITLE_PAL) $(ART_SPR) $(ART_TILE) | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# crt0 が必ず先頭に来るようにリンク順を固定する
$(IHX): $(BUILDDIR)/crt0.rel $(AOBJ) $(COBJ)
	$(CC) $(LDFLAGS) -o $@ $(BUILDDIR)/crt0.rel $(filter-out $(BUILDDIR)/crt0.rel,$(AOBJ)) $(COBJ)

$(ROM): $(IHX) $(TITLE_BIN) tools/ihx2rom.py
	python3 tools/ihx2rom.py $(IHX) $(ROM) 64 $(TITLE_SEG):$(TITLE_BIN)

run: $(ROM)
	openmsx -machine $(MACHINE) -carta $(ROM) -romtype ASCII8

clean:
	rm -rf $(BUILDDIR)

check: $(ROM)
	sh tools/smoketest.sh $(ROM)
