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
	.globl	_vdp_wblock
	.globl	_vdp_blklen		; 実体は vdp.c 側 (C の変数として置く)
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
	call	wait_ce			; 実行中のコマンドと CPU の VRAM アクセスを混ぜない
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
;   R#36..R#46 (DX,DY,NX,NY,CLR,ARG,CMD) の11バイトを送る。
;   呼ぶ前に vdp_wait_cmd() でコマンド終了を待つこと。
;
;   注意: R#17 の間接ポート(0x9B)へ OTIR で流すと VDP が取りこぼす。実際に
;   NY が化けて画面全体を塗る暴走コマンドになり、CE が下りずハングした。
;   通常のレジスタ書き込み(値→レジスタ番号)なら間隔が十分に空くので確実。
;-------------------------------------------------------------------
_vdp_cmd::
	di
	ld	c, #VDP_ADDR
	ld	b, #36			; 送り先レジスタ番号
vdp_cmd_lp:
	ld	a, (hl)			; 値
	out	(c), a
	inc	hl
	ld	a, b			; レジスタ番号
	or	a, #0x80
	out	(c), a
	inc	b
	ld	a, b
	cp	a, #47			; R#46 まで
	jr	NZ, vdp_cmd_lp
	; R#46 を書いた瞬間にコマンドが走り出すが、S#2 の CE が立つまでには
	; わずかに遅れがある。ここで待たずに次の vdp_wait_cmd を呼ぶと CE=0 を
	; 見て「終わった」と誤判定し、実行中のコマンドの上から DY/NX/NY を
	; 書き換えてしまう (エンジンがこの3つを作業用に更新するため暴走する)。
	ld	b, #16
vdp_cmd_dly:
	djnz	vdp_cmd_dly
	ei
	ret

;-------------------------------------------------------------------
; 内部: コマンド実行中 (S#2 の CE) が下りるまで待つ。呼び出し側は DI 済みであること。
;   R#15=2 のまま割り込みが入ると BIOS が S#0 を読めず暴走するので DI のまま待つ。
;   壊すのは A/F のみ (HL は温存する = fastcall の引数を跨げる)。
;-------------------------------------------------------------------
wait_ce:
	ld	a, #2
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 2
	out	(VDP_ADDR), a
wait_ce_lp:
	in	a, (VDP_ADDR)
	and	a, #0x01		; CE
	jr	NZ, wait_ce_lp
	xor	a
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 0 に戻す
	out	(VDP_ADDR), a
	ret

;-------------------------------------------------------------------
; void vdp_wait_cmd(void);
;-------------------------------------------------------------------
_vdp_wait_cmd::
	di
	call	wait_ce
	ei
	ret

;-------------------------------------------------------------------
; void oam_flush(void);
;   スプライト属性テーブル (0x7600) へ 32面ぶん 128バイトを一括転送。
;   PUT SPRITE 相当を毎面呼ぶより速い (プロトの FLUSH05 と同じ狙い)。
;-------------------------------------------------------------------
_oam_flush::
	di
	call	wait_ce			; 実行中のコマンドと CPU の VRAM アクセスを混ぜない
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
; void vdp_wblock(const uint8_t *src) __z88dk_fastcall;
;   _vdp_blklen バイトを VRAM へ連続転送する。書き込みアドレスは呼ぶ前に
;   vdp_setwrite() で設定しておくこと。1バイトずつ関数呼び出しするより桁違いに速い。
;-------------------------------------------------------------------
_vdp_wblock::
	di
	call	wait_ce
	ld	de, (_vdp_blklen)	; D=256バイト単位の回数 / E=端数
	ld	c, #VDP_DATA
	ld	a, d
	or	a, a
	jr	Z, wblk_tail
wblk_pages:
	ld	b, #0			; B=0 は otir では 256 回
	otir
	dec	d
	jr	NZ, wblk_pages
wblk_tail:
	ld	a, e
	or	a, a
	jr	Z, wblk_done
	ld	b, e
	otir
wblk_done:
	ei
	ret

;-------------------------------------------------------------------
; void psg_write(uint16_t rv) __z88dk_fastcall;   H=値 / L=レジスタ番号
; uint8_t psg_read(uint8_t r) __z88dk_fastcall;
;   PSG (AY-3-8910) は 0xA0=レジスタ選択 / 0xA1=書き込み / 0xA2=読み出し。
;-------------------------------------------------------------------
PSG_ADDR	= 0xA0
PSG_WDATA	= 0xA1
PSG_RDATA	= 0xA2

_psg_write::
	ld	a, l
	out	(PSG_ADDR), a
	ld	a, h
	out	(PSG_WDATA), a
	ret

_psg_read::
	ld	a, l
	out	(PSG_ADDR), a
	in	a, (PSG_RDATA)
	ld	l, a
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
	.globl	_psg_write
	.globl	_psg_read
_vdp_wpal::
	ld	a, l
	out	(VDP_PALT), a
	ret
