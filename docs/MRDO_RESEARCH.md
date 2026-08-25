# Mr. Do!（初代・1982 Universal）挙動の再調査メモ

SDCC 移植にあたり、岩（＝Mr. Do! の apple）・敵の追跡・穴掘りの挙動を一次情報で確認し直した記録。
**本作に採用した結論と、採用しなかった点の理由**まで書く。仕様の正は `DESIGN.md`、
本ファイルはその根拠。

## 参照した資料

| 資料 | 種別 | 得られたもの |
|---|---|---|
| [Retro Reversing - Mr Do! Source Code (Game Boy)](https://www.retroreversing.com/mrdo) | **逆アセンブル解析** | ルーチン名レベルの実装挙動（最重要） |
| [Arcade Club - Mr. Do!](https://www.arcadeclub.co.uk/games/mr-do/) | 遊び方 | リンゴの割れ条件・玉・クリア条件 |
| [tips.retrogames.com - Mr. Do!](http://tips.retrogames.com/gamepage/mrdo.html) | 攻略 | リンゴ落としの手順・敵の追跡・掘り屋化・得点 |
| [Wikipedia - Mr. Do!](https://en.wikipedia.org/wiki/Mr._Do!) | 概要 | 「潜り込む／押す」でリンゴを落とす |
| [StrategyWiki - Mr. Do!/Gameplay](https://strategywiki.org/wiki/Mr._Do!/Gameplay) | 攻略 | （403 で直接取得できず。他資料と検索要約で補完） |

> Game Boy 版は移植であって基板そのものではない。ただしルーチン名と状態機械が残っており、
> 「支え判定は何を見ているか」「揺れは何フレームか」といった**アーケードの攻略記述では
> 分からない粒度**が取れる。攻略資料と矛盾しない範囲で採用した。

## 1. 穴掘り・移動

| 事実 | 出典 | 本作での扱い |
|---|---|---|
| 方向転換はセル境界に整列している時だけ（`(X+8)&15==0` かつ `(Y+8)&15==0`）。16x16 グリッドの曲がり角ゲート。 | Retro Reversing | **採用**。プロトの「最寄り節点へスナップ（最大8pxワープ）」をやめ、整列するまで現在の向きで進み、整列した瞬間に曲がる方式へ変更。入力は保持する。 |
| 土を食い進む時は速度ペナルティがある（`EATWALL` が `SPEED`/`SPDCOW` で減速）。既掘の坑道は等速（`EATUNNEL` は no-op）。 | Retro Reversing | **採用**。坑道 60px/s、掘削中 40px/s（分子加算式）。 |
| セル種別は 0=壁/土, 1=坑道, 2=チェリー, 3=リンゴ, 4=ボーナス の1バイトマップ（`BYTESCREEN`）で持つ。 | Retro Reversing | 同等（`dug[]` ＋ `powder[]`）。将来ダイヤ/風呂を種別に足す。 |
| 掘った跡は坑道より細く、隣接坑道の間に土が残る。 | 画面・プロトで確認済 | **維持**（本作の肝）。自機幅より内側2pxだけ抜く。 |

## 2. 岩（Mr. Do! の apple）

| 事実 | 出典 | 本作での扱い |
|---|---|---|
| 状態機械は **待機 → 揺れ → 落下 → 割れ** の4状態（`APPLEWAIT` / `APPLEJIG` / `APPLEFALL` / `APPLESPLIT`）。 | Retro Reversing | **採用**。 |
| 待機中の支え判定は **真下の「タイル」を見る**（`APPLEWAIT` = "support test on tiles below"）。アクター（自機）は支えに数えていない。 | Retro Reversing | **採用（設計変更）**。§4 参照。 |
| 揺れは **60フレーム**（`APPLEJIG` = "60-frame wobble"）＝約1秒。 | Retro Reversing | **採用**。プロトの 45ティック(0.75s) から変更。 |
| 「リンゴの左右を掘り、続けて真下を掘って通り過ぎる。リンゴは**自分の後ろに**落ちる」 | tips.retrogames | 上の2点（タイル支え＋1秒の猶予）から自然に再現される。 |
| 「Mr. Do または敵は、縦穴の縁からリンゴを押し出せる」＝押しは既掘の坑道内で行い、縦穴へ押し出すと落ちる。 | Arcade Club / StrategyWiki | **採用**。押し先が開きセルの時だけ押せる（土へ掘りながら押す旧仕様は廃止）。 |
| **自身の高さより大きく落ちると割れて消える**。 | Arcade Club / Wikipedia | **採用**。1セル落下なら割れず存続。 |
| 落下中のリンゴは縦一列の敵を**まとめて**潰す。得点は 1/2/3/4/5体で 1000/2000/4000/6000/8000。 | tips.retrogames | 潰しは採用。得点表は本作の得点設計が別なので参考のみ。 |
| Mr. Do 自身も落下リンゴで潰されて1ミス。 | Arcade Club | **採用**（自機も潰される）。 |
| 敵はリンゴを避けるが、落ちてくるリンゴの下で固まることがある。真下で待ちすぎると掘り屋化する。 | tips.retrogames | 敵実装時に反映（余力があれば）。 |

## 3. 敵の追跡アルゴリズム

| 事実 | 出典 | 本作での扱い |
|---|---|---|
| 節点で `FINDEXITS` が周囲を走査し、**通れる方向のビットマスク**を返す（bit3=上, bit0=右, bit2=下, bit1=左）。 | Retro Reversing | **採用**（4方向の開きマスクを作る）。 |
| `WORKEXITS` が向きを決める。**通れるなら現在の向きを維持**し、そうでなければ選び直す。さらに **約1/8の確率で強制的に選び直す**（`RND2 & 7 == 0`）。候補探索は4件までに制限。 | Retro Reversing | **採用**。「常に最短方向へ曲がる」プロトの `CHASEDIR` は直線的すぎるので、これに置き換える。 |
| 通常の敵は坑道しか通らないが、**掘り屋（Driller Dino）へ変異**して土を掘り進む。変異中は一瞬止まる。後半面ほど変異が早い。 | tips.retrogames / Wikipedia | **採用**（面進行で変異確率を上げる）。 |
| 掘り屋の掘進は `EATWALL` 経由なので**掘っている間は遅い**。 | Retro Reversing | **採用**（自機と同じ減速則）。 |
| アルファモンスター（文字を集めると出る強敵）は**まっすぐ自機へ向かう**。既掘の坑道を走り続ければ捕まらない。 | tips.retrogames | 本作にアルファモンスター相当は無いが、「追跡が強い個体」の挙動として温存。 |

> 追跡の**方向バイアス**（自機の方へ向かう傾向）の具体式は解析資料に無い。攻略資料の
> 「敵は自機の方向へ掘る」「アルファは直進」から、本作では
> **開き方向の候補から自機に近づく向きを優先し、同点なら現在の向きを維持、1/8で乱択**とする。

## 4. 岩の支えと敵の挙動は **Dig Dug 準拠**（2026-08-25 に差し戻し）

いったん Mr. Do! に寄せて次の2点を変更したが、**作者の判断で Dig Dug 準拠へ戻した**。
経緯を残しておく。

### 4-1. 岩の支え

- 一時採用した Mr. Do! 版: 支え判定はタイルだけを見る（`APPLEWAIT` = "support test on tiles below"）。
  自機は支えにならない。
- **確定版（Dig Dug）**: **真下の升目に自機が居る間、岩は揺れも落下もしない。好きなだけ待てる。**
  自機が離れた瞬間に揺れ始め、約0.75秒で落ちる。
  出典: *"The rock will only start to fall once Dig Dug moves out from under it. Until such time, he can
  wait underneath the rock for as long as he likes."* / *"You can hold a rock in place by staying directly
  under it. Wait there until a Pooka or Fygar comes up from below, move to either side, and the rock will
  crush the enemy."*（[Getting Good: Dig Dug](https://primetimeamusements.com/getting-good-dig-dug/)）
- 押し・落下距離での割れ・潰しは **Mr. Do! 準拠のまま**。

### 4-2. 敵の掘り

- 一時採用した Mr. Do! 版: 掘り屋（Driller Dino）へ変異して土を掘り進む。
- **確定版（Dig Dug）**: **敵は自分では掘らない**。透明になって土をすり抜け、トンネルに入ると
  実体化する。土の中では倒せず、トンネルの中では倒せる。斜め移動する。
  出典: *"After a few seconds of losing sight of the player, monsters can transform into ghostly forms to
  move through solid earth until they reach another tunnel"* / *"travel diagonally"* /
  *"once a ghost enters a tunnel, it can be killed"*（[Pookapedia](https://digdug.fandom.com/wiki/Dig_Dug)）
- 坑道の中を歩くときの向きの決め方は **Mr. Do! 準拠のまま**（現在の向きを維持・約1/8で選び直し）。

### 4-3. 方向転換

- 一時採用した Mr. Do! 版: 整列していなければ曲がれない（入力は保持）。
- **確定版（Dig Dug）**: 曲がるのは節点だけだが、**半歩ずれていれば最寄りの節点へ寄せてから曲がる**。
  Dig Dug も *"can only make ninety degree turns at particular points along an invisible grid"* だが、
  「整列するまで曲がらない」だけでは止まった位置がずれていると詰んでしまうため。

## 5. 本作であえて Mr. Do! と変える点

- **武器**: 跳ねる玉ではなく**消費式ダイナマイト**（本作の遊びの軸）。玉の跳ね返り
  （節点判定・乱数摂動）は移植しない。
- **勝利条件**: チェリー全収集ではなく**画面の90%採掘**。
- **敵の出現**: 画面中央の巣から、ステージごとの総数まで**時間差で湧く**
  （2026-08-24 の仕様確定。当初案の Dig Dug 式初期配置は採らない）。移動 AI は Mr. Do! 式のまま。
- **生存管理**: 体温オーバーヒート＋風呂（Mr. Do! に相当物なし）。
