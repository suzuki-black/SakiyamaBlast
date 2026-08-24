;-------------------------------------------------------------------
;  SakiyamaBlast - VDP / OAM / マッパー 低レベルルーチン
;
;  引数は __z88dk_fastcall (16bit=HL / 8bit=L) で受ける。SDCC の既定呼出規約は
;  バージョンで揺れるため、手書きasmは必ず fastcall を明示して固定する。
;
;  ポート 0x99 への2バイト書き込みは first/second フリップフロップを使うので、
;  途中で BIOS の割り込みハンドラ (ステータス読み) が入ると壊れる。→ 必ず DI/EI で囲む。
;-------------------------------------------------------------------
	.module lowlevel

	.globl	_vdp_wreg
	.globl	_vdp_setwrite
	.globl	_vdp_wdata
	.globl	_vdp_cmd
	.globl	_vdp_wait_cmd
	.globl	_oam_flush
	.globl	_bank_select
	.globl	_oam

VDP_DATA	= 0x98			; VRAM データ
VDP_ADDR	= 0x99			; レジスタ/アドレス設定・ステータス読み
VDP_PALT	= 0x9A			; パレット
VDP_INDR	= 0x9B			; R#17 間接書き込み (コマンドレジスタ用)

ASC8_A000	= 0x7800		; 0xA000-0xBFFF のバンクレジスタ

	.area _CODE

;-------------------------------------------------------------------
; void vdp_wreg(uint16_t rv) __z88dk_fastcall;   H=値 / L=レジスタ番号
;-------------------------------------------------------------------
_vdp_wreg::
	di
	ld	a, h
	out	(VDP_ADDR), a
	ld	a, l
	or	a, #0x80
	out	(VDP_ADDR), a
	ei
	ret

;-------------------------------------------------------------------
; void vdp_setwrite(uint16_t addr) __z88dk_fastcall;
;   VRAM 書き込みアドレスを設定 (R#14 = A16..A14 も合わせて設定)。
;-------------------------------------------------------------------
_vdp_setwrite::
	di
	ld	a, h
	rlca				; bit7,bit6 (=A15,A14) を bit1,bit0 へ
	rlca
	and	a, #0x03
	out	(VDP_ADDR), a
	ld	a, #0x8E		; R#14
	out	(VDP_ADDR), a
	ld	a, l
	out	(VDP_ADDR), a
	ld	a, h
	and	a, #0x3F
	or	a, #0x40		; 書き込みフラグ
	out	(VDP_ADDR), a
	ei
	ret

;-------------------------------------------------------------------
; void vdp_wdata(uint8_t v) __z88dk_fastcall;   L=値
;-------------------------------------------------------------------
_vdp_wdata::
	ld	a, l
	out	(VDP_DATA), a
	ret

;-------------------------------------------------------------------
; void vdp_cmd(const uint8_t *p) __z88dk_fastcall;
;   R#36..R#46 の11バイト (DX,DY,NX,NY,CLR,ARG,CMD) を間接書き込みで一気に送る。
;   呼ぶ前に vdp_wait_cmd() でコマンド終了を待つこと。
;-------------------------------------------------------------------
_vdp_cmd::
	di
	ld	a, #36			; bit7=0 -> 書き込み後オートインクリメント
	out	(VDP_ADDR), a
	ld	a, #0x91		; R#17
	out	(VDP_ADDR), a
	ld	b, #11
	ld	c, #VDP_INDR
	otir
	ei
	ret

;-------------------------------------------------------------------
; void vdp_wait_cmd(void);   コマンド実行中 (S#2 の CE) が下りるまで待つ。
;   R#15=2 のまま割り込みが入ると BIOS が S#0 を読めず暴走するので DI のまま待つ。
;-------------------------------------------------------------------
_vdp_wait_cmd::
	di
	ld	a, #2
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 2
	out	(VDP_ADDR), a
wait_ce:
	in	a, (VDP_ADDR)
	and	a, #0x01		; CE
	jr	NZ, wait_ce
	xor	a
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 0 に戻す
	out	(VDP_ADDR), a
	ei
	ret

;-------------------------------------------------------------------
; void oam_flush(void);
;   スプライト属性テーブル (0x7600) へ 32面ぶん 128バイトを一括転送。
;   PUT SPRITE 相当を毎面呼ぶより速い (プロトの FLUSH05 と同じ狙い)。
;-------------------------------------------------------------------
_oam_flush::
	di
	ld	a, #1			; R#14 = 1 (A16..A14) -> 0x4000 基点
	out	(VDP_ADDR), a
	ld	a, #0x8E
	out	(VDP_ADDR), a
	xor	a			; 下位 = 0x00
	out	(VDP_ADDR), a
	ld	a, #0x76		; (0x76 & 0x3F) | 0x40 = 0x76 -> 0x4000+0x3600 = 0x7600
	out	(VDP_ADDR), a
	ld	hl, #_oam
	ld	c, #VDP_DATA
	ld	b, #128
	otir
	xor	a			; R#14 を 0 に戻す
	out	(VDP_ADDR), a
	ld	a, #0x8E
	out	(VDP_ADDR), a
	ei
	ret

;-------------------------------------------------------------------
; void bank_select(uint8_t seg) __z88dk_fastcall;
;   0xA000-0xBFFF のデータ窓に ROM セグメント seg を割り当てる。
;-------------------------------------------------------------------
_bank_select::
	ld	a, l
	ld	(#ASC8_A000), a
	ret

;-------------------------------------------------------------------
; void vdp_wpal(uint8_t v) __z88dk_fastcall;   パレットポートへ1バイト
;   R#16 (パレットポインタ) を設定してから RB/G の2バイトを続けて送る。
;-------------------------------------------------------------------
	.globl	_vdp_wpal
_vdp_wpal::
	ld	a, l
	out	(VDP_PALT), a
	ret
