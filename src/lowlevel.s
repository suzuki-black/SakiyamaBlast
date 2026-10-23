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
	.globl	_bank_cur
	.globl	_pcm_ctrl
	.globl	_blob_read
	.globl	_blob_off
	.globl	_blob_dst
	.globl	_blob_len
	.globl	_blob_seg
	.globl	_pcm_play
	.globl	_pcm_src
	.globl	_pcm_left
	.globl	_pcm_line
	.globl	_vdp_wblock
	.globl	_vdp_blklen		; 実体は vdp.c 側 (C の変数として置く)
	.globl	_oam
	.globl	_oam_len

VDP_DATA	= 0x98			; VRAM データ
VDP_ADDR	= 0x99			; レジスタ/アドレス設定・ステータス読み
VDP_PALT	= 0x9A			; パレット
VDP_INDR	= 0x9B			; R#17 間接書き込み (コマンドレジスタ用)

ASC8_A000	= 0x7800		; 0xA000-0xBFFF のバンクレジスタ
PCM_DATA	= 0xA4			; turbo R の PCM データ
PCM_CTRL	= 0xA5			; 同 制御 (bit0 BUFF / bit1 MUTE)
TAIKO_SEG	= 12			; 和太鼓の波形を置いた ROM セグメント

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
	; 決め打ちで待つと毎回その時間を捨てることになるので、CE が立ったのを
	; 確認できたら即抜ける。短すぎて立つ前に終わったコマンドのために上限も置く。
	ld	a, #2
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 2
	out	(VDP_ADDR), a
	ld	b, #24
vdp_cmd_rise:
	in	a, (VDP_ADDR)
	and	a, #0x01		; CE が立った?
	jr	NZ, vdp_cmd_up
	djnz	vdp_cmd_rise
vdp_cmd_up:
	xor	a
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 0 に戻す
	out	(VDP_ADDR), a
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
	ld	a, (_oam_len)		; 使っている面のぶんだけ送る
	ld	b, a
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
	; **控えを先に書く。** 割り込みが間に入っても、ISR が戻す先は「これから
	; 向ける先」になる。順番を逆にすると、ISR が古い向き先へ戻してしまい、
	; 戻ってきたコードが別のセグメントを読む。
	ld	(#_bank_cur), a
	ld	(#ASC8_A000), a
	ret

;-------------------------------------------------------------------
; void pcm_ctrl(uint8_t v) __z88dk_fastcall;   turbo R の PCM 制御ポート
;   bit0 BUFF / **bit1 MUTE (0 にすると PSG も FM も消える)** / bit3 SEL
;-------------------------------------------------------------------
_pcm_ctrl::
	ld	a, l
	out	(#PCM_CTRL), a
	ret

;-------------------------------------------------------------------
; void blob_read(void);
;   **窓の中の表を RAM へ写す。** 引数は変数で渡す (_blob_seg / _blob_off /
;   _blob_dst / _blob_len)。SDCC の呼び出し規約に左右されないため。
;
;   窓側のコードから呼んでよい。窓を切り替えている間に実行されるのは
;   **固定領域にあるこのルーチンだけ**で、戻る前に元のセグメントへ戻す。
;
;   **控え (_bank_cur) も一緒に書き替える。** 写している途中で和太鼓の割り込みが
;   入ると、ISR は控えを見て窓を戻すので、控えが古いままだと**続きを別の
;   セグメントから読む**ことになる。
;-------------------------------------------------------------------
_blob_read::
	ld	a, (#_bank_cur)
	ld	c, a			; 戻す先
	ld	a, (#_blob_seg)
	ld	(#_bank_cur), a
	ld	(#ASC8_A000), a
	ld	hl, (#_blob_off)
	ld	de, (#_blob_dst)
	ld	a, (#_blob_len)
	ld	b, a
blob_lp:
	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de
	djnz	blob_lp
	ld	a, c
	ld	(#_bank_cur), a
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

;-------------------------------------------------------------------
; 陽炎 (かげろう)。V9938 のライン割り込みで R#18 を1画面に何度か書き替え、
; ラスタごとに横位置を ±1〜2px ずらす。**VRAM は一切触らない。**
;
;   _haze_n   分割数 (0 = 何もしない)
;   _haze_i   次に処理するバンド
;   _haze_row 各バンドの R#19 の値   _haze_adj 各バンドで R#18 に書く値
;
; H.KEYI (0xFD9A) へフックする。**BIOS が割り込み源を判定する前**に呼ばれるので、
; S#1 の FH (bit0) を見て、ライン割り込みならここで処理して帰る。
; 垂直帰線割り込みのときは何もせず BIOS へ帰す。
;
; **R#15 は必ず 0 へ戻す。** 戻さないと BIOS が S#0 を読めず暴走する
; (この落とし穴は wait_ce のコメントにも書いてある)。
;-------------------------------------------------------------------
H_KEYI		= 0xFD9A

_haze_hook::
	di
	ld	hl, #_haze_isr
	ld	a, #0xC3		; jp nn
	ld	(#H_KEYI), a
	ld	(#H_KEYI + 1), hl
	ei
	ret

_haze_isr:
	push	af
	push	bc
	push	hl
	ld	a, #1
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 1 (S#1 を選ぶ)
	out	(VDP_ADDR), a
	in	a, (VDP_ADDR)		; S#1。読むと FH が落ちる
	ld	c, a
	xor	a
	out	(VDP_ADDR), a
	ld	a, #0x8F		; R#15 = 0 へ戻す
	out	(VDP_ADDR), a
	bit	0, c			; FH = ライン割り込みか
	jp	Z, haze_out
	; ------------------------------------------------------------------
	; **和太鼓 (PCM) が鳴っている間は、こちらが割り込みを使う。**
	; 4行ごとに1サンプル = 3,925Hz。波形は ROM のセグメントにあるので、
	; 窓をそこへ向けて1バイト読み、**すぐ元へ戻す** (描画の途中で割り込んでも
	; 壊れないように。戻す先は bank_select が控えてある)。
	; ------------------------------------------------------------------
	ld	a, (#_pcm_play)
	or	a, a
	jr	Z, haze_bands
	ld	a, (#_bank_cur)
	ld	c, a			; 戻す先を控える
	ld	a, #TAIKO_SEG
	ld	(#ASC8_A000), a
	ld	hl, (#_pcm_src)
	ld	a, (hl)
	out	(#PCM_DATA), a
	inc	hl
	ld	(#_pcm_src), hl
	ld	a, c
	ld	(#ASC8_A000), a		; 窓を戻す
	ld	hl, (#_pcm_left)
	dec	hl
	ld	(#_pcm_left), hl
	ld	a, h
	or	a, l
	jr	Z, pcm_done
	ld	a, (#_pcm_line)		; 次は4行先 (= 3,925Hz。2行では割り込みが間に合わない)
	add	a, #4
	cp	a, #244			; 上端の手前は割り込めない。次のフレームの頭から
	jr	C, pcm_line_ok
	xor	a, a
pcm_line_ok:
	ld	(#_pcm_line), a
	out	(VDP_ADDR), a
	ld	a, #0x93		; R#19
	out	(VDP_ADDR), a
	jp	haze_out
pcm_done:
	xor	a, a
	ld	(#_pcm_play), a
	ld	a, #0x80		; D/A を中心へ戻す (鳴り止めでパチッと言わせない)
	out	(#PCM_DATA), a
	ld	a, (#_haze_n)		; 陽炎が使っていたなら返す
	or	a, a
	jr	Z, pcm_ie_off
	xor	a, a
	ld	(#_haze_i), a
	ld	a, (#_haze_row)
	out	(VDP_ADDR), a
	ld	a, #0x93
	out	(VDP_ADDR), a
	jp	haze_out
pcm_ie_off:
	ld	a, #0x06		; IE1 を切る (GRAPHIC4 のまま)
	out	(VDP_ADDR), a
	ld	a, #0x80		; R#0
	out	(VDP_ADDR), a
	jp	haze_out
haze_bands:
	ld	a, (#_haze_n)
	or	a, a
	jr	Z, haze_out
	ld	c, a			; C = 分割数
	ld	hl, (#_haze_hits)	; 走った回数 (計測用)
	inc	hl
	ld	(#_haze_hits), hl
	; いまのバンドの値を R#18 へ
	ld	hl, #_haze_adj
	ld	a, (#_haze_i)
	add	a, l
	ld	l, a
	jr	NC, haze_a1
	inc	h
haze_a1:
	ld	a, (hl)
	out	(VDP_ADDR), a
	ld	a, #0x92		; R#18
	out	(VDP_ADDR), a
	; 次のバンドを R#19 へ仕込む
	ld	a, (#_haze_i)
	inc	a
	cp	a, c
	jr	C, haze_a2
	xor	a			; 一周したので先頭へ (次のフレームで効く)
haze_a2:
	ld	(#_haze_i), a
	ld	hl, #_haze_row
	add	a, l
	ld	l, a
	jr	NC, haze_a3
	inc	h
haze_a3:
	ld	a, (hl)
	out	(VDP_ADDR), a
	ld	a, #0x93		; R#19
	out	(VDP_ADDR), a
haze_out:
	pop	hl
	pop	bc
	pop	af
	ret
