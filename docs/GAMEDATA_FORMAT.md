# msx-gamedata 形式 仕様書 (formatVersion 1)

MSX 用ゲーム1本ぶんの**画面・パレット・スプライト・背景タイル・キャラ情報**を、
1つの JSON にまとめるための形式。パターンエディタなど外部ツールとの受け渡しに使う。

- 実体: リポジトリ直下の **`gamedata.json`**
- 生成: `make gamedata`（`tools/export_gamedata.py`）
- 生成元: **`assets/` の PNG が絵の正**。パレットは `tools/palette.py`、面の掘り抜きに使う
  数字は `src/field.c` から読む。キャラ名や動作の別など、PNG から分からない付属情報は
  エクスポータ内の表で持つ。

> **編集の向き**: 双方向。
>
> - 書き出し `make gamedata` … `assets/*.png` → `gamedata.json`
> - 取り込み `make gamedata-import` … `gamedata.json` の `pixels` → `assets/*.png`
>
> 取り込みは `pixels` **だけ**を見る。`lineColors` などの派生データ（各項目の `derived`
> に列挙）は無視するので、エディタ側で更新しそこねても害はない。逆に `pixels` を
> 更新せずに `lineColors` だけ直しても何も反映されない。
>
> パレット（`palette.entries`）は取り込みでは**変更しない**。ゲーム側の
> `tools/palette.py` と食い違っていたら警告を出すだけなので、色を変えたときは
> `tools/palette.py` を直すこと（`build/palette.h` はそこから自動生成される）。
>
> 取り込みは**検証に1つでも引っかかったら PNG を一切書かずに中止する**。MSX で
> 表示できない絵をそのまま入れると、原因の分かりにくい不具合になるため。
> 検証内容は §9 を参照。`--dry-run` を付けると検証だけ行う。

---

## 0. 全体の構成

```
{
  "format": "msx-gamedata",
  "formatVersion": 1,
  "generator":  { ... },      // 何が作ったか
  "game":       { ... },      // タイトル・対象機種
  "screen":     { ... },      // 画面モードと VRAM 配置
  "palette":    { ... },      // 16色
  "paletteVariants": [ ... ], // 面ごとの色替え
  "spriteFormat":   { ... },  // スプライトの持ち方と制約
  "spritePatterns": [ ... ],  // スプライトの絵
  "tileFormat":     { ... },  // タイルの持ち方
  "tiles":          [ ... ],  // 背景タイルの絵
  "characters":     [ ... ],  // キャラと動作（右移動①…）
  "procedural":     [ ... ],  // 絵を持たず手続きで描くもの
  "stages":         { ... },  // 面の構成
  "sources":        { ... }   // 元ファイルの場所
}
```

すべてのキーは省略可能ではない（`formatVersion` が上がるまでは全部入る）。
名前は `{"ja": ..., "en": ...}` の2言語で持つ。片方しか無い場合は `null` を入れる。

---

## 1. 絵の表し方（最重要）

**スプライトもタイルも、絵は `pixels` で持つ。** 16行の文字列で、1文字＝1ピクセル。

| 文字 | 意味 |
|---|---|
| `.` | 透明（描かない） |
| `0`〜`F` | パレット番号（16進1桁）。`palette.entries[].index` に対応 |

```
"pixels": [
  "................",
  ".....FFFFFF.....",
  "....FFFFFFFF....",
  ...
]
```

- 行数は必ず 16、各行の長さも必ず 16。
- テキストなので diff が取れ、手で直せる。**この形式が正**。

### 1.1 スプライトの制約（守らないと MSX で表示できない）

MSX の **mode-2 スプライトは1行につき1色**しか持てない。したがって

> **`pixels` の1行に、`.` 以外の文字を2種類以上置いてはならない。**

エディタはこの制約を編集時に強制すること。破ると、ビルド時にその行で
一番多い色へ丸められる（`tools/png2art.py` の挙動）。

`spritePatterns[].lineColors` は各行の色を並べた**派生データ**で、`pixels` から求められる。
`derived` 配列に載っているキーは**編集しても無視される**。

### 1.2 タイルの制約

タイルは SCREEN 5 の 4bpp ビットマップなので、**1行に何色でも置ける**。
ただし画面全体で使える色は 16色（`palette`）だけ。

---

## 2. `screen`

画面モードと VRAM の配置。エディタは主に `resolution` `colorSpace` `sprite` を見る。

| キー | 意味 |
|---|---|
| `mode` / `vdpMode` | `"SCREEN5"` / `"GRAPHIC 4"` |
| `resolution` | `[256, 212]` |
| `bitsPerPixel` | `4` |
| `colorsOnScreen` | `16` |
| `colorSpace` | `{channels:3, bitsPerChannel:3, totalColors:512}` — **各色は R/G/B とも 0〜7** |
| `bytesPerLine` | `128`（2ピクセル＝1バイト） |
| `playfield` | 盤面の矩形と升目 `[16, 11]`、1升 `[16, 16]` |
| `hud` | HUD の帯の矩形 |
| `vram` | ビットマップ／スプライト色表／スプライト属性表／スプライトパターンのアドレス |
| `sprite` | `mode:2` / `size:[16,16]` / `planes:32` / `activePlanes:10` / `colorsPerLine:1` / `parkY:212` |

`sprite.parkY` は「画面外へ逃がすときの Y」。**216 は mode 2 の行終端マーカー**なので使えない。

---

## 3. `palette`

`entries` は 16個。**`rgb3` が正**（各0〜7）。`rgb8` は 0〜255 に伸ばした表示用の目安で、
派生データなので編集しても無視される。

```json
{ "index": 9, "rgb3": [7,3,2], "rgb8": [255,109,73],
  "name": {"ja":"ネズミ","en":"Rat"}, "symbol": "COL_RAT", "note": null }
```

- `index` **0番は特別**。SCREEN 5 では背景色レジスタ R#7 の色で出る（本作は黒）。
  スプライトでは透明を意味する。**絵の中で 0 を「黒」として使わないこと**（`1` を使う）。
- `symbol` はゲーム側の C の定数名。`src/main.c` の `set_game_palette()` と
  `tools/palette.py` に一致していなければならない。

### 3.1 `paletteVariants`

面ごとの色替え。`overrides` に載っている番号だけを差し替える。

```json
{ "id": "dirt_1", "name": {"ja":"硬い岩盤の灰", ...},
  "appliesTo": "stages where (stage-1) % 3 == 1",
  "overrides": [ { "index": 6, "rgb3": [3,3,2] } ] }
```

本作では**土の色（6番）だけ**が面ごとに変わる。エディタはプレビューの背景色を
これで切り替えられる。

---

## 4. `spritePatterns`

絵そのもの。1件＝1パターン。

| キー | 意味 |
|---|---|
| `id` | 一意な文字列。`characters[].animations[].frames[].pattern` から参照される |
| `patternNumber` | ROM 上のスプライト絵番号（OAM に書く値は ×4） |
| `size` | `[16, 16]`（固定） |
| `pixels` | §1 の16行 |
| `lineColors` | 各行の色（**派生**） |
| `derived` | 派生キーの一覧。ここに載るキーは編集しても無視される |
| `source` | 元 PNG とセル位置 |

## 5. `tiles`

背景タイル。1件＝1タイル。

| キー | 意味 |
|---|---|
| `id` / `tileNumber` / `size` / `pixels` / `source` | スプライトと同じ |
| `name` | 表示名 |
| `kind` | `pickup`（拾う）/ `bonus`（ボーナス）/ `field`（地形） |
| `description` | 何をするものか |
| `drawInsetPx` | **0 なら16x16全部を描く。2 なら外周2pxを描かない** |

`drawInsetPx: 2` は、坑道の上に重ねて描くもの（火薬・風呂・ボーナス）に付く。
本作の坑道は「隣が土の辺だけ内側2px」で彫ってあり、**外周2pxに薄い土の膜が残る**。
ここを塗り潰すと膜が消えて見た目が壊れるため、**絵は内側 12x12 に収める**こと。

`drawInsetPx: 0` は升目まるごと置き換えるもの（横穴・ダイヤモンド鉱石）。

---

## 6. `characters`

キャラと、その動作（アニメーション）。**ここが付属情報の本体**。

```json
{
  "id": "miner",
  "name": { "ja": "丹野3時", "en": "Tanno Sanji" },
  "role": "player",
  "description": "先山（坑内で採炭を主導する炭鉱夫）。主人公。ヘルメットにカンテラ。",
  "spritePlanes": [0],
  "defaultPattern": "miner_00",
  "animations": [
    { "id": "walk_right", "name": {"ja":"右移動","en":"Walk right"}, "loop": true,
      "frames": [
        { "pattern": "miner_00", "durationFrames": 8,
          "label": {"ja":"右移動1","en":"Walk right 1"}, "status": "final" },
        { "pattern": "miner_00", "durationFrames": 8,
          "label": {"ja":"右移動2","en":"Walk right 2"}, "status": "placeholder" }
      ] }
  ]
}
```

| キー | 意味 |
|---|---|
| `role` | `player` / `enemy` / `item` / `obstacle` |
| `spritePlanes` | このキャラが使うスプライト面。複数体出るものは複数 |
| `defaultPattern` | 動作が決まらないときに使う絵 |
| `animations[].id` | `walk_right` `walk_left` `walk_up` `walk_down` `dig` `miss` `fuse` `idle` `wobble` `float_*` |
| `animations[].loop` | 繰り返すか |
| `frames[].pattern` | `spritePatterns[].id` への参照 |
| `frames[].durationFrames` | 60fps でのフレーム数 |
| `frames[].label` | **「右移動①」に相当する表示名** |
| `frames[].status` | `final`（描き上がり）/ `placeholder`（未着手。今は既存の絵を指している） |

**`status: "placeholder"` は「枠だけ用意してある」印**。現状は各キャラ1枚しか絵が無いため、
方向ごと・コマごとの枠は用意しつつ、中身は同じ絵を指している。エディタで描いたら
新しい `spritePatterns` を足し、`frames[].pattern` をそちらへ向け、`status` を `final` にする。

---

## 7. `procedural`

**絵（パターン）を持たず、コードが矩形塗りで描くもの**。エディタで編集する対象ではないが、
色の意味を知るために載せてある。

| id | 内容 |
|---|---|
| `dirt` | 土。面ごとに色が変わる（`paletteVariants`） |
| `tunnel` | 掘り済みの升目。隣が土の辺だけ内側2px（薄い土の膜が残る） |
| `blast` | 爆発の瞬間、十字の5升を1色で塗る |
| `hud` | 画面下36pxの帯。文字は本体 BIOS の 8x8 フォント |

---

## 8. `stages`

面の構成。エディタで直接いじるものではないが、プレビューに使える。

- `carving.digits` — 面番号の掘り抜きに使う**数字の形**。10個（0〜9）× 9行 × 5文字。
  `#` が掘る升目。1桁は `singleDigitCol` から、2桁は `twoDigitCols` の位置に置く。
- `perStage` — 面ごとの数値（ダイヤの割合、火薬の数、敵の総数、岩の数、クリア条件）。
  式は文字列で持つ（`"4 + stage"` など）。

---

## 9. エディタ側への注意

1. **`pixels` が正。** `lineColors` `rgb8` など `derived` に載るキーは読み取り専用。
2. **スプライトは1行1色。** 編集時に強制すること。
3. **色は 0〜7 の3bit。** 中間色は作れない。`rgb8` を直接編集しない。
4. **0番は透明／背景色。** 黒を塗りたいときは `1` を使う。
5. **`drawInsetPx: 2` のタイルは内側 12x12 に収める。**
6. パレット番号を入れ替えると全部の絵の意味が変わる。`symbol` を手掛かりに、
   ゲーム側の `tools/palette.py` も必ず同時に直すこと（`src/main.c` が読む
   `build/palette.h` は `tools/palette.py` から自動生成されるので、直す場所は1つ）。
7. **`source.cell` は PNG の中の位置**なので、順番を入れ替えたり増減させたりしたら
   0 から始まる連番に振り直すこと。増減させた場合はゲーム側の `Makefile`
   （`ART_SPR_NAMES` / `ART_TILE_NAMES`）も直す必要がある。

### 9-1. 取り込み時の検証

`make gamedata-import`（`tools/import_gamedata.py`）が実際に弾く条件。
**1つでも該当したら PNG を書かずに中止**する。

| 種別 | 条件 | 対象 |
|---|---|---|
| エラー | `format` が `msx-gamedata` でない／`formatVersion` が 1 でない | 全体 |
| エラー | `pixels` が16行でない、または16文字でない行がある | スプライト・タイル |
| エラー | `.` でも `0`〜`F` でもない文字がある | スプライト・タイル |
| エラー | `0` で塗っている（0番は透明。黒は `1`） | スプライト・タイル |
| エラー | パレットに定義の無い番号を使っている | スプライト・タイル |
| エラー | 1行に2色以上ある | スプライトのみ |
| エラー | `source.cell` が 0 からの連番でない | スプライト・タイル |
| エラー | 枚数が `Makefile` の名前の数と合わない | スプライト・タイル |
| 警告 | `drawInsetPx: 2` なのに外周2pxを塗っている（そこは描かれない） | タイル |
| 警告 | `palette.entries` の色がゲーム側と違う（取り込みでは直さない） | パレット |

---

## 10. 変更履歴

| formatVersion | 変更 |
|---|---|
| 1 | 初版。画面／パレット／スプライト／タイル／キャラと動作／手続き描画／面構成を収録。 |

`formatVersion` を上げるのは**既存のエディタが読めなくなる変更**をしたときだけ。
キーを増やすだけなら上げない（読み手は知らないキーを無視すること）。
