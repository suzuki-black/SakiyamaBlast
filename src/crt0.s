;-------------------------------------------------------------------
;  SakiyamaBlast - MSX MegaROM (ASCII8) 起動コード / crt0
;
;  ROM 配置 (ASCII8, 8KB セグメント):
;    seg0 -> 0x4000-0x5FFF  固定: ROM ヘッダ + コード
;    seg1 -> 0x6000-0x7FFF  固定: コード
;    seg2 -> 0x8000-0x9FFF  可変: 窓2 (win2_select() で差し替え)。起動時は
;                                 seg2 = 面の組み立て。main() が遊ぶ側へ向け直す
;    seg3 -> 0xA000-0xBFFF  可変: データ窓 (bank_select() で差し替え)
;  RAM/スタックはページ3 (0xC000-0xF37F)。
;-------------------------------------------------------------------
	.module crt0
	.globl	_main
	.globl	l__INITIALIZER
	.globl	s__INITIALIZED
	.globl	s__INITIALIZER

; --- ASCII8 マッパーのバンクレジスタ (書き込みアドレス) ---
ASC8_4000	= 0x6000
ASC8_6000	= 0x6800
ASC8_8000	= 0x7000
ASC8_A000	= 0x7800

; --- BIOS ---
ENASLT		= 0x0024
RSLREG		= 0x0138
EXPTBL		= 0xFCC1

	.area _HEADER (ABS)
	.org	0x4000
	.db	0x41			; 'A'
	.db	0x42			; 'B'
	.dw	init			; INIT
	.dw	0x0000			; STATEMENT
	.dw	0x0000			; DEVICE
	.dw	0x0000			; TEXT
	.dw	0x0000			; reserved
	.dw	0x0000
	.dw	0x0000

	.area _CODE
init::
	di
	ld	sp, #0xF380		; BIOS ワークエリア直下
	; セグメント 0..3 を 0x4000/0x6000/0x8000/0xA000 へ明示配置。
	; 起動直後は全レジスタ0 (=0x8000 に seg0 が見えている) なので必ず設定する。
	xor	a
	ld	(#ASC8_4000), a
	inc	a
	ld	(#ASC8_6000), a
	inc	a
	ld	(#ASC8_8000), a
	inc	a
	ld	(#ASC8_A000), a
	call	page2_same_slot		; ページ2をカートスロットへ (0x8000-0xBFFF を有効化)
	call	gsinit			; 初期値付きグローバルを ROM から RAM へコピー
	ei
	call	_main
_exit::
	di
	halt
	jr	_exit

; ページ2 (0x8000-0xBFFF) をページ1と同じスロットへ切り替える定石ルーチン。
; BIOS は 0x4000 のヘッダしか見ないため、32KB 超のカートは自分でページ2を有効化する。
page2_same_slot:
	call	RSLREG			; A = 基本スロットレジスタ
	rrca
	rrca
	and	a, #0x03		; ページ1の基本スロット番号
	ld	c, a
	ld	b, #0
	ld	hl, #EXPTBL
	add	hl, bc
	ld	a, (hl)
	and	a, #0x80		; 拡張スロットか
	or	a, c
	ld	c, a
	inc	hl			; EXPTBL+4 = SLTTBL
	inc	hl
	inc	hl
	inc	hl
	ld	a, (hl)
	and	a, #0x0C		; ページ1の拡張スロット番号
	or	a, c
	ld	h, #0x80		; ページ2を指定
	jp	ENASLT

; --- SDCC 標準のエリア順序 ---
	.area _HOME
	.area _CODE
	.area _INITIALIZER
	.area _GSINIT
	.area _GSFINAL

	.area _DATA
	.area _INITIALIZED
	.area _BSEG
	.area _BSS
	.area _HEAP

	.area _GSINIT
gsinit::
	ld	bc, #l__INITIALIZER
	ld	a, b
	or	a, c
	jr	Z, gsinit_done
	ld	de, #s__INITIALIZED
	ld	hl, #s__INITIALIZER
	ldir
gsinit_done:

	.area _GSFINAL
	ret
