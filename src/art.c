/* SakiyamaBlast - 生成された絵データの置き場 (実体はここだけが持つ) */
#include "art.h"
#include "sprites.h"      /* assets/sprites.png から生成 */
#include "tiles.h"        /* assets/tiles.png から生成 */

/* スプライトの枚数。main.c の転送ループがここを見ることで、絵を足したときの
   転送漏れを防ぐ。**PAT_SPLIT を足したとき、ループが PAT_BAT までのままだったため
   割れた岩のパターンが VRAM へ一度も転送されず、ゴミが表示されていた。** */
uint8_t art_pattern_count(void)
{
    return ART_SPR_N;
}

const uint16_t *art_pattern(uint8_t i)
{
    return art_spr_pat[i];
}

const uint8_t *art_colours(uint8_t i)
{
    return art_spr_col[i];
}

const uint8_t *art_tile_of(uint8_t i)
{
    return art[i];
}
