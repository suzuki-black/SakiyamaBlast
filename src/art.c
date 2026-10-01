/*
 * SakiyamaBlast - 絵の参照口
 *
 * **絵の実体はここには無い。** 0xA000 の窓は 8KB しかないので、絵は ROM の
 * 専用セグメント (ART_TILE_SEG / ART_SPR_SEG) に置いてある。ここが返すのは
 * **窓を向けたときの番地**だけで、実際に読むときは窓を切り替える必要がある。
 *
 *   - 背景タイル: draw_tile() が切り替える (src/field.c)
 *   - スプライト: spr_pattern_rows() / spr_color_lines() が切り替える (src/sprite.c)
 *
 * **切り替える側は固定領域か RAM に置くこと。** 窓を絵へ向けている間は、
 * 窓にあるコード (BANKED_SRC) が見えなくなる。
 */
#include "art.h"
#include "sprites.h"      /* assets/sprites.png から生成 (枚数と名前だけ) */
#include "tiles.h"        /* assets/tiles.png から生成 (枚数と名前だけ) */

#define ART_WIN 0xA000                      /* 窓の先頭 */
#define ART_COL_OFF (ART_SPR_N * 32)        /* 形の後ろに色が並ぶ */

/* スプライトの枚数。main.c の転送ループがここを見ることで、絵を足したときの
   転送漏れを防ぐ。**PAT_SPLIT を足したとき、ループが PAT_BAT までのままだったため
   割れた岩のパターンが VRAM へ一度も転送されず、ゴミが表示されていた。** */
uint8_t art_pattern_count(void)
{
    return ART_SPR_N;
}

const uint16_t *art_pattern(uint8_t i)
{
    return (const uint16_t *)(ART_WIN + ((uint16_t)i << 5));
}

const uint8_t *art_colours(uint8_t i)
{
    return (const uint8_t *)(ART_WIN + ART_COL_OFF + ((uint16_t)i << 4));
}

const uint8_t *art_tile_of(uint8_t i)
{
    return (const uint8_t *)(ART_WIN + ((uint16_t)i << 7));
}
