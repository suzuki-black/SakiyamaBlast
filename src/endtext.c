/*
 * SakiyamaBlast - エンドロールの文面
 *
 * **日本語の原案を作者が決め、それを英訳したもの** (2026-10-24)。ROM の文字は
 * BIOS のフォントなので**英数字だけ**。漢字は自前の18字しか無く、本文には使えない。
 * **全部大文字**にしてあるのは作者の指定。
 *
 * 置き場は**曲と同じセグメント14**。窓 (0xA000) に置くには大きく、固定領域にも入らない。
 * `src/ending.c` が `blob_read()` で1行 (33バイト) ずつ RAM へ写して描く。
 *
 * 文面を直すときはここだけ直せばよい。**行数を変えたら `END_ROLL_N` も直す**
 * (合っていなければコンパイルが止まる)。1行は**32文字まで** (256px / 8px)。
 * 倍角 (EROW_ATTR_T) は16文字まで。
 *
 * 物語の筋は作者の原案:
 *   1. 丹野3時はシフトを終え、愛する妻と娘が待つ炭鉱長屋へ向かう
 *   2. 妻の手料理と刺身の盛り合わせと大好きな日本酒も彼を待っている
 *   3. 彼は購入したばかりのテレビで大相撲中継を見ながら晩酌をするだろう
 *   4. 彼が掘った黒ダイヤは、やがて町の灯りになり、あの茶の間のテレビを光らせる
 *   5. ——おかえりなさい
 *
 * 4番目は**労働と暮らしを一本の線でつなぐ**ための締め。3番目でテレビに寄ったあと、
 * そのテレビを光らせているのが彼の掘った石炭だと種明かしをして、最後の一言で
 * 家の中へ戻る。
 *
 * 献辞の「丹野3時」は**架空の名**。実在の先山の名前は ROM にも README にも入れない
 * (資料にだけ載せる。`DESIGN.md` の公開前 TODO)。
 */
#include "endtext.h"

#define N EROW_ATTR_N
#define H EROW_ATTR_H
#define T EROW_ATTR_T

const roll_row end_roll[] = {
    /* --- 物語 --- */
    { N, "" },
    { N, "" },
    { N, "" },
    { N, "TANNO 3JI FINISHES HIS SHIFT" },
    { N, "AND WALKS BACK TO THE ROW HOUSE" },
    { N, "WHERE HIS WIFE AND DAUGHTER" },
    { N, "ARE WAITING FOR HIM." },
    { N, "" },
    /* **"HER" にしない。** 直前が "HIS WIFE AND DAUGHTER" なので、英語では
       どちらを指すか文法上きまらない (作者の指摘、2026-10-25)。
       "HIS WIFE'S COOKING IS ON THE TABLE," は35文字で1行に入らないため、
       **語はそのままで2行に割ってある** (縮めて言い換えない)。 */
    { N, "HIS WIFE'S COOKING" },
    { N, "IS ON THE TABLE," },
    { N, "A PLATE OF SASHIMI BESIDE IT," },
    { N, "AND THE SAKE HE LOVES." },
    { N, "" },
    { N, "TONIGHT HE WILL POUR A CUP" },
    { N, "IN FRONT OF THE BRAND-NEW TV," },
    { N, "WATCHING THE SUMO." },
    { N, "" },
    { N, "THE BLACK DIAMOND HE DUG TODAY" },
    { N, "WILL BECOME THE TOWN'S LIGHT" },
    { N, "AND LIGHT THAT VERY SCREEN." },
    { N, "" },
    { N, "WELCOME HOME." },
    { N, "" },
    { N, "" },
    { N, "" },
    /* --- 題 --- */
    { T, "KURODAIYA" },
    { N, "- SAKIYAMA TANNO 3JI -" },
    { N, "" },
    { N, "" },
    /* --- クレジット --- */
    { H, "ORIGINAL CONCEPT" },
    { N, "SUZUKI-BLACK" },
    { N, "" },
    { H, "DIRECTION" },
    { N, "SUZUKI-BLACK" },
    { N, "" },
    { H, "GAME DESIGN" },
    { N, "SUZUKI-BLACK" },
    { N, "CLAUDE CODE" },
    { N, "" },
    { H, "PROGRAM" },
    { N, "CLAUDE CODE" },
    { N, "" },
    { H, "GRAPHICS" },
    { N, "CLAUDE CODE" },
    { N, "" },
    { H, "SOUND" },
    { N, "CLAUDE CODE" },
    { N, "" },
    { N, "(ANTHROPIC CLAUDE)" },
    { N, "" },
    { N, "" },
    { H, "MUSIC" },
    { N, "JOBAN TANKO-BUSHI" },
    { N, "TRADITIONAL" },
    { N, "" },
    { N, "DOWN IN A COAL MINE" },
    { N, "J. B. GEOGHEGAN, 1872" },
    { N, "" },
    { N, "" },
    { H, "BUILT WITH" },
    { N, "SDCC / OPENMSX" },
    { N, "" },
    { H, "FONT" },
    { N, "SHINONOME 16 (BOLD)" },
    { N, "PUBLIC DOMAIN" },
    { N, "" },
    { N, "" },
    { N, "" },
    /* --- 献辞 ---
       **否定から始めない。** はじめ "TANNO 3JI NEVER EXISTED." と書いたが、
       ゲーム中ずっと動かしてきた主人公を最後に打ち消す読み味になり、断り書きのように
       なってしまった (作者の指摘、2026-10-25)。**名を伏せたのは実在したからで、話は逆。**
       「こしらえた名前だ」と言い換えて、待っていた家族で結ぶ (物語と `WELCOME HOME.`
       につながる)。一人称は直前のクレジットの作者の声。 */
    { N, "TANNO 3JI IS A NAME I MADE UP" },
    { N, "FOR A MAN WHO REALLY LIVED." },
    { N, "THIS GAME IS FOR HIM," },
    { N, "FOR EVERY SAKIYAMA," },
    { N, "AND FOR THE FAMILIES WHO WAITED." },
    { N, "" },
    { N, "" },
    { H, "THANK YOU FOR PLAYING" },
    { N, "" },
    /* **抜け方を書いておく。** 流し終わるとここで止まるので、案内が無いと
       何を押せばよいか分からない (作り替える前の静止画面には出ていた)。 */
    { N, "PUSH SPACE KEY" }
};

/* 行数が `END_ROLL_N` と合っていなければ、ここでコンパイルが止まる
   (合わないと、読み出しが表の外へ出て化けた行が流れる)。 */
typedef char end_roll_count_check[
    (sizeof(end_roll) / sizeof(end_roll[0]) == END_ROLL_N) ? 1 : -1];
