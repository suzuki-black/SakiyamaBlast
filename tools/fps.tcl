# ゲーム中の fps を測る。openMSX の -script に渡す。
#
#   FPSOUT=out.txt FRAMEN=<_frame_n の番地> CFGSTAGE=<_cfg_start_stage の番地> \
#   TEMP=<_temp の番地> openmsx ... -script tools/fps.tcl
#
# **エミュレータ内の経過時間で割る。** `throttle off` だと実時間は意味を持たないので、
# `machine_info time` (エミュレート上の秒) を使う。1秒あたりの frame_n が fps。
#
# **「遊んでいる最中」を体温で見分ける。** タイトル画面でも frame_n は進むので、
# `temp` が動き始めたことを確かめてから数え始める (CLAUDE.md)。
# さらに**掘らせ続ける**: 立っているだけだと描画の重い場面を通らない。
set renderer none
set throttle off

proc u16 {addr} {
    expr {[debug read memory $addr] + 256 * [debug read memory [expr {$addr+1}]]}
}

set ::t0 0
set ::f0 0

proc warm {} {
    # 体温が上がり始めたら = 面が始まっている
    if {[debug read memory $::env(TEMP)] == 0} { after time 0.2 warm; return }
    set ::t0 [machine_info time]
    set ::f0 [u16 $::env(FRAMEN)]
    after time 20 done
}

proc done {} {
    set dt [expr {[machine_info time] - $::t0}]
    set df [expr {[u16 $::env(FRAMEN)] - $::f0}]
    set f [open $::env(FPSOUT) w]
    puts $f [format "%.2f fps (%d フレーム / %.2f 秒)" [expr {$df / $dt}] $df $dt]
    close $f
    exit
}

proc ready {} {
    if {[debug read {VDP regs} 1] != 98} { after time 0.1 ready; return }
    # 右へ掘り続けさせる (スティックを倒しっぱなし)
    keymatrixdown 8 0x80
    after time 1.0 warm
}

after time 6   {debug write memory $::env(CFGSTAGE) 1; keymatrixdown 8 0x01}
after time 6.2 {keymatrixup 8 0x01}
after time 6.5 ready
