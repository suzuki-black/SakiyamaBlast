;-------------------------------------------------------------------
;  SakiyamaBlast - BIOS 呼び出しラッパ
;  ページ0 (0x0000-0x3FFF) の BIOS はカート起動後もそのまま使える。
;  BIOS は IX/IY を壊すことがあるので退避する。
;-------------------------------------------------------------------
	.module bios
	.globl	_gtstck
	.globl	_gttrig
	.globl	_snsmat

GTSTCK		= 0x00D5		; A=装置番号 -> A=方向 0..8
GTTRIG		= 0x00D8		; A=装置番号 -> A=0 or 0xFF
SNSMAT		= 0x0141		; A=行番号 -> A=その行の8キー (ビット0=押されている)

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

