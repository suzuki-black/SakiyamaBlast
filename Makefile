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
CFLAGS    := -mz80 --std-c99 --sdcccall 1 --opt-code-speed -I$(SRCDIR)
LDFLAGS   := -mz80 --no-std-crt0 --code-loc 0x4010 --data-loc 0xC000

CSRC      := $(wildcard $(SRCDIR)/*.c)
ASRC      := $(SRCDIR)/crt0.s $(SRCDIR)/lowlevel.s $(SRCDIR)/bios.s
COBJ      := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.rel,$(CSRC))
AOBJ      := $(patsubst $(SRCDIR)/%.s,$(BUILDDIR)/%.rel,$(ASRC))

ROM       := $(BUILDDIR)/$(NAME).rom
IHX       := $(BUILDDIR)/$(NAME).ihx

# 開発用の既定機種。turbo R の実 BIOS ROM を持っていれば MACHINE=Panasonic_FS-A1GT
MACHINE   ?= C-BIOS_MSX2+_JP

.PHONY: all run check clean

all: $(ROM)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.rel: $(SRCDIR)/%.s | $(BUILDDIR)
	$(AS) -g -o $@ $<

$(BUILDDIR)/%.rel: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# crt0 が必ず先頭に来るようにリンク順を固定する
$(IHX): $(BUILDDIR)/crt0.rel $(AOBJ) $(COBJ)
	$(CC) $(LDFLAGS) -o $@ $(BUILDDIR)/crt0.rel $(filter-out $(BUILDDIR)/crt0.rel,$(AOBJ)) $(COBJ)

$(ROM): $(IHX) tools/ihx2rom.py
	python3 tools/ihx2rom.py $(IHX) $(ROM) 64

run: $(ROM)
	openmsx -machine $(MACHINE) -carta $(ROM) -romtype ASCII8

clean:
	rm -rf $(BUILDDIR)

check: $(ROM)
	sh tools/smoketest.sh $(ROM)
