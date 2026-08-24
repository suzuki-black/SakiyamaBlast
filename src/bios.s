;-------------------------------------------------------------------
;  SakiyamaBlast - BIOS 呼び出しラッパ
;  ページ0 (0x0000-0x3FFF) の BIOS はカート起動後もそのまま使える。
;  BIOS は IX/IY を壊すことがあるので退避する。
;-------------------------------------------------------------------
	.module bios
	.globl	_gtstck
	.globl	_gttrig

GTSTCK		= 0x00D5		; A=装置番号 -> A=方向 0..8
GTTRIG		= 0x00D8		; A=装置番号 -> A=0 or 0xFF

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
