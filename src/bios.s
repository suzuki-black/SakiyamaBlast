;-------------------------------------------------------------------
;  SakiyamaBlast - BIOS 呼び出しラッパ
;  ページ0 (0x0000-0x3FFF) の BIOS はカート起動後もそのまま使える。
;  BIOS は IX/IY を壊すことがあるので退避する。
;-------------------------------------------------------------------
	.module bios
	.globl	_gtstck
	.globl	_gttrig
	.globl	_snsmat
	.globl	_msxver
	.globl	_chgcpu
	.globl	_getcpu
	.globl	_rdslt_go
	.globl	_rd_slot
	.globl	_rd_addr
	.globl	_expbyte

GTSTCK		= 0x00D5		; A=装置番号 -> A=方向 0..8
GTTRIG		= 0x00D8		; A=装置番号 -> A=0 or 0xFF
SNSMAT		= 0x0141		; A=行番号 -> A=その行の8キー (ビット0=押されている)
; turbo R の CPU 切り替え。**MSX2+ 以前には存在しないので、機種を見てから呼ぶこと。**
CHGCPU		= 0x0180		; A=モード (bit0-1) | 0x80 でLEDも変える
GETCPU		= 0x0183		; -> A=いまのモード
MSXVER		= 0x002D		; 0=MSX1 1=MSX2 2=MSX2+ 3=turbo R
RDSLT		= 0x000C		; A=スロット番号 HL=番地 -> A=その1バイト
EXPTBL		= 0xFCC1		; 基本スロット4つ。bit7 = 拡張されている

	.area _CODE

; uint8_t gtstck(uint8_t dev) __z88dk_fastcall;
_gtstck::
	push	ix
	push	iy
	ld	a, l
	call	GTSTCK
	pop	iy
	pop	ix
	ld	l, a
	ret

; uint8_t gttrig(uint8_t dev) __z88dk_fastcall;
_gttrig::
	push	ix
	push	iy
	ld	a, l
	call	GTTRIG
	pop	iy
	pop	ix
	ld	l, a
	ret

; uint8_t snsmat(uint8_t row) __z88dk_fastcall;
;   キーマトリクスを1行読む。押されているキーのビットが 0 になる。
;   行2: bit7=B bit6=A / 行8: bit0=SPACE bit4=左 bit5=上 bit6=下 bit7=右
_snsmat::
	push	ix
	push	iy
	ld	a, l
	call	SNSMAT
	pop	iy
	pop	ix
	ld	l, a
	ret


; uint8_t msxver(void);
;   ページ0 はカート起動後も BIOS のままなので、そのまま読める。
;   0=MSX1 / 1=MSX2 / 2=MSX2+ / 3=turbo R
_msxver::
	ld	a, (#MSXVER)
	ld	l, a
	ret

; void chgcpu(uint8_t mode) __z88dk_fastcall;
;   mode: 0=Z80 / 1=R800 ROM / 2=R800 DRAM。0x80 を足すと本体のLEDも変わる。
;   **turbo R 以外で呼ぶと 0x0180 は別の意味なので暴走する。必ず msxver() で確かめる。**
_chgcpu::
	push	ix
	push	iy
	ld	a, l
	call	CHGCPU
	pop	iy
	pop	ix
	ret

; uint8_t getcpu(void);
_getcpu::
	push	ix
	push	iy
	call	GETCPU
	pop	iy
	pop	ix
	ld	l, a
	ret


; uint8_t rdslt_go(void);
;   **引数は変数で渡す** (_rd_slot / _rd_addr)。SDCC は --sdcccall 1 で最初の引数を
;   レジスタに載せるので、スタックから取るつもりで書くと黙って別の値を読む
;   (実際に、内蔵 MSX-MUSIC を見つけられない形で引っかかった。2026-10-23)。
;   他のスロットを覗く唯一の安全な手段。自分のページを差し替えないので、
;   読んでいる間もこのコードは生きている。
_rdslt_go::
	push	ix
	push	iy
	ld	a, (#_rd_slot)
	ld	hl, (#_rd_addr)
	call	RDSLT
	pop	iy
	pop	ix
	ld	l, a
	ret

; uint8_t expbyte(uint8_t i) __z88dk_fastcall;
;   EXPTBL[i]。bit7 が立っていれば、その基本スロットは拡張されている。
_expbyte::
	ld	h, #0
	ld	bc, #EXPTBL
	add	hl, bc
	ld	l, (hl)
	ret
