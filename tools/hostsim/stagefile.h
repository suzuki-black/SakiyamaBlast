/*
 * SakiyamaBlast - 面データの読み書き (ホスト側の道具だけ。ROM には入らない)
 *
 * 形式の仕様は docs/STAGE_FORMAT.md。
 */
#ifndef STAGEFILE_H
#define STAGEFILE_H

#include <stdio.h>

/* 組み立て済みの盤面を面データとして書き出す (build_stage() の後に呼ぶ)。
   `src` が NULL なら「面番号 + lseed から組み立てた」、そうでなければ
   「その面データを読んだ」と `generator` に書く (どちらも読み込みでは無視する)。 */
void stage_dump(FILE *fp, unsigned lseed, const char *src);

/* 面データを読んで盤面を直接作る。build_stage() の代わり。
   不備があればその場で何が悪いかを stderr へ書いて exit(1)。 */
void stage_load(const char *path);

/* 盤面を決める変数を**すべて**一覧にする (往復の検証用)。
   `--stage N --state` と `--load <その面のダンプ> --state` が
   一致すれば、面データに取りこぼしが無いと言える。 */
void stage_state(FILE *fp);

#endif
