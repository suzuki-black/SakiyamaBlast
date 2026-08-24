# SakiyamaBlast

昭和初期・常磐炭鉱の坑内を舞台にした1画面固定の「掘り＋発破」アクション（MSX turbo R / SDCC）。

仕様の正は **[DESIGN.md](DESIGN.md)**、移植の経緯と保つべき中核は **[HANDOFF_SDCC.md](HANDOFF_SDCC.md)**。
`proto/digbomb.msxb` は FunctionBASIC 製プロト（メカニクス検証済みの参照実装。移植元コードではない）。

## ビルド・実行

```bash
make            # build/sakiyama.rom を作る
make run        # openMSX で起動
make clean
```

必要なもの: `sdcc` (4.6 で確認)、`python3`、`openmsx`。

実機 turbo R の BIOS ROM を持っていれば機種を指定できる。既定は C-BIOS（フリー BIOS）。

```bash
make run MACHINE=Panasonic_FS-A1GT
```

## ターゲットと ROM 構成

- **実装 = SDCC で C → ROM 化**。コードは `-mz80` で吐くので turbo R 専用にはせず MSX2/2+ 実機でも動く
  （SCREEN 5 を使うため MSX2 以降は必須）。turbo R では R800 でそのまま速く走る。
- **ROM = MegaROM (ASCII8) / 64KB**。8KB セグメント。

| セグメント | アドレス | 用途 |
|---|---|---|
| 0 | 0x4000-0x5FFF | 固定。ROM ヘッダ + コード |
| 1 | 0x6000-0x7FFF | 固定。コード |
| 2 | 0x8000-0x9FFF | 固定。コード |
| 3.. | 0xA000-0xBFFF | 可変データ窓。`bank_select(seg)` で差し替え |

固定コードは 0x4000-0x9FFF（24KB）に収める。超えると `tools/ihx2rom.py` がエラーで止める。
RAM/スタックはページ3（`--data-loc 0xC000`、SP=0xF380）。

## 画面

SCREEN 5（GRAPHIC 4, 256x212, 16色/512パレット, 16x16 mode-2 スプライト）。

| VRAM | 用途 |
|---|---|
| 0x0000 | ビットマップ（ページ0） |
| 0x7400 | スプライト色テーブル（mode 2） |
| 0x7600 | スプライト属性テーブル（OAM） |
| 0x7800 | スプライトパターン |

盤面は 16列 x 11行（16px セル）= 256x176px、その下 y=176-211 が HUD。

## ソース構成

| ファイル | 役割 |
|---|---|
| `src/crt0.s` | ROM ヘッダ、ASCII8 マッパー設定、ページ2のスロット有効化、gsinit |
| `src/lowlevel.s` | VDP レジスタ/VRAM/コマンドエンジン、OAM 一括転送、バンク切り替え |
| `src/bios.s` | BIOS 呼び出しラッパ（GTSTCK / GTTRIG） |
| `src/vdp.c` | SCREEN 5 初期化、パレット、矩形塗り（LMMV）、VRAM 転送 |
| `src/sprite.c` | OAM 複製の管理、16x16 パターン/色の登録 |
| `src/game.h` | 盤面定数（COLS/ROWS/速度/色/方向） |
| `src/main.c` | メインループ、自機の移動と掘り |
| `tools/ihx2rom.py` | .ihx → MegaROM イメージ変換（配置チェック付き） |

## 実装状況

- [x] ROM 起動・SCREEN 5 初期化・パレット
- [x] OAM 直書きによるスプライト表示
- [x] 自機のスムーズ移動、曲がり角のグリッドスナップ
- [x] Dig-Dug 流の掘り跡（内側2px 抜き＝隣接トンネル間に薄い土の膜が残る）
- [x] DUG 記録はセル完全整列時のみ（掠めただけでは掘れた扱いにしない）
- [ ] ダイナマイト（設置/導火/十字爆発/自爆）と弾薬（火薬）
- [ ] 岩（Mr.Do! 準拠：押しは開きセルのみ・支えルール・落下距離で割れ）
- [ ] ダイヤモンドブロック
- [ ] 体温オーバーヒート＋風呂
- [ ] 敵（Dig-Dug 式の初期配置＋坑道 AI）
- [ ] 90%採掘クリア判定・HUD・ステージ進行
- [ ] テーマスプライト（丹野3時／ネズミ／コウモリ／風呂／火薬／ダイヤ／岩）
- [ ] BGM / 効果音（オリジナル曲）

## 実装メモ（つまずいた点）

- V9938 のコマンドコードは **`1000`=LMMV / `1100`=HMMV**。`0xC0` を LMMV と取り違えると
  バイト単位の塗りになり、SCREEN 5 では1ピクセルおきに色0が残る（縦縞になる）。
- 16x16 スプライトの VRAM 並びは「左半分16バイト → 右半分16バイト」。行交互に書くと絵が崩れる。
  作画は `spr_pattern_rows()`（16行 x 16bit の表）を使う。
- ポート 0x99 への2バイト書き込みは first/second フリップフロップを使うので、BIOS 割り込みが
  割って入ると壊れる。VDP アクセスは DI/EI で囲む。
- スプライトのパーク Y は **212**。216 は mode 2 の行終端マーカーなので使わない。
- 32KB を超えるカートは BIOS がページ2を有効化してくれない。crt0 で ENASLT を呼ぶ。

## 権利方針

絵・音・曲・キャラ名はすべてオリジナル。Mr.Do! / Dig Dug からはメカニクス（著作対象外）のみ。
詳細は `DESIGN.md §権利方針`。
