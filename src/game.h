/* SakiyamaBlast - 盤面・共有状態・モジュール間インタフェース */
#ifndef GAME_H
#define GAME_H

#include "msx.h"

/* --- 盤面 --- */
#define COLS      16          /* 升目列 (16px セル) */
#define ROWS      11          /* 升目行 */
#define NCELL     (COLS * ROWS)
#define CELL      16
#define FIELD_H   176         /* 盤面の高さ px (= ROWS * CELL) */
#define HUD_Y     176         /* HUD 帯の上端 */
#define SCR_H     212

#define PW        15          /* スプライト幅-1 */
#define XMAX      (255 - PW)
#define YMAX      (FIELD_H - CELL)

/* 速度は分子加算式。毎フレーム加算し SPD_UNIT に達するごとに 1px 進む。
   Mr. Do! は既掘の坑道は等速・未掘の土を食い進む間は減速する (EATWALL の速度ペナルティ)。
   60fps で 坑道 60px/s / 掘削 40px/s。
   分母を 12 にしてあるのは、敵の速度を 5px/s 刻みで調整できるようにするため。 */
#define SPD_UNIT   24
#define SPD_TUNNEL 24         /* 60px/s */
#define SPD_DIG    16         /* 40px/s */

/* 岩。押し・落下距離での割れ・潰しは Mr. Do! の apple 準拠だが、**支え判定は Dig Dug 準拠**で、
   真下の升目に自機が居る間は揺れも落下もしない (好きなだけ待てる)。自機が離れた瞬間に
   揺れ始め、揺れ切ると落ちる。 */
#define ROCK_MAX      8
#define ROCK_JIG_FRAMES 45      /* 揺れ (約0.75秒。DESIGN の当初値) */
/* **押して縁から出したときだけ、揺れを短くする。**
   掘って支えを抜いたときの 45フレーム は「逃げる猶予」だが、押している最中は
   その 45フレーム のあいだに岩が45ドット＝約3升も進んでしまい、**穴を通り過ぎて
   落ちない。** Mr. Do! では押して縁を越えた瞬間に落ちるので、そちらへ寄せる。 */
#define ROCK_JIG_PUSH   6       /* 押して落とすときの揺れ (0.1秒) */
#define ROCK_FALL_SPD 2       /* 落下 px/frame (1セル=8フレーム) */

/* 敵 (ネズミ / コウモリ)。
   出現: 画面中央の「巣」(ステージ開始時から掘られている升目) から湧く。ステージ毎に
   決められた総数まで、一定時間ごと、または画面上の敵が全滅した瞬間に次が出てくる。
   総数を出し切ったらそれ以上出ない。
   移動: 坑道の中は Mr. Do! 式＝節点で通れる方向のマスクを作り、通れるなら現在の向きを
   維持、約1/8で強制的に選び直す (WORKEXITS / RND2 & 7 == 0)。
   すり抜け: **敵は自分では掘らない** (Dig Dug 準拠)。一定時間で透明になって土の中を
   自機へ向かって斜めに直進し、トンネルに入ると実体化する。透明な間は倒せない。
   出典: "monsters can transform into ghostly forms to move through solid earth until they
   reach another tunnel" / "travel diagonally" / "rematerialize as soon as they get into a
   tunnel"。 */
/*
 * 敵は**プール制**（パックマン方式）。面ごとに決まった数の敵が居て、
 * 倒しても消えてなくならず、洞窟でしばらく静養してからまた出てくる。
 * 「合計 N 体を出し切ったら終わり」ではないので緊張感が途切れない。
 *   - 面の頭は全員が洞窟に居て、SPAWN_GAP ごとに1体ずつ出てくる（短い間隔）
 *   - 倒された敵はそのぶん長く洞窟にこもる（RESPAWN_*）。新規と再出現で間隔が違う
 * 同時に画面へ出るのはプールの数まで。ENEMY_MAX はその上限。
 */
#define ENEMY_MAX     6       /* プールの上限 = 同時に出る上限 */
#define POOL_BASE     3       /* 面1のプール数 */
#define POOL_STEP     3       /* 何面ごとに1体増やすか */
#define SPAWN_GAP     90      /* 洞窟から次の1体が出るまで (1.5秒)。新規はこの間隔 */
#define RESPAWN_BASE  600     /* 倒された敵の静養 (面1 = 10秒) */
#define RESPAWN_STEP   30     /* 面が進むごとに短くなる */
#define RESPAWN_MIN   240     /* 下限 (4秒) */
#define EN_RED        0
#define EN_PINK       1
#define EN_CYAN       2
#define EN_ORANGE     3
#define EN_KINDS      4

#define PINK_AHEAD    4       /* 桃が読む先読み升数 */
#define CYAN_AHEAD    2       /* 水が基準にする先読み升数 */
#define CLYDE_NEAR    6       /* 橙がこれより近づくと隅へ逃げる (盤面16x11 に合わせた) */

/* 散開と追跡の切り替え。散開の間は各自の隅へ向かうので、group で固まらずに散る。 */
#define SCATTER_FRAMES 300    /* 5秒 */
#define CHASE_FRAMES  1200    /* 20秒 */

/* 色ごとの速さ。赤がいちばん速く、橙がいちばん遅い。 */
#define SPD_RED      14       /* 35px/s */
#define SPD_PINK     12       /* 30px/s */
#define SPD_CYAN     12       /* 30px/s */
#define SPD_ORANGE   10       /* 25px/s */

/* 追跡の「遊び」。パックマンのゴーストは完全に決定的だが、本作は盤面が毎回違って
   パターンを覚えられないので、少しだけ揺らして一直線になりすぎないようにする。
   rnd8() & 値 == 0 の確率なので 15 なら 1/16。 */
#define STRAY_ALL    15

/* 透明化して未掘の土をすり抜けている間は、**素の速さの半分以下**に落とす
   (Dig Dug 準拠)。土の中を突っ切るほうが速いと、坑道を掘る意味が薄れるため。 */
#define SPD_GHOST     3       /* 7.5px/s */
#define GHOST_BASE    600     /* 透明化までのフレーム数 (面1 = 10秒) */
#define GHOST_STEP    20      /* 面が進むごとに早くなる */
#define GHOST_MIN     300     /* 高次面での下限 (5秒) */
/* 切羽圧。掘った升の数だけ透明化までの時間を縮める。
   「掘るほど危険が増すが、掘らないとクリアできない」という本作の芯を、面番号ではなく
   プレイヤー自身の行為に結び付けるための項。開始時の掘り抜きは数えない。 */
#define GHOST_PRESSURE 3      /* 1升掘るごとに縮むフレーム数 (0.05秒) */

#define GHOST_PAUSE   20      /* 透明になる瞬間は一拍止まる */
/* 湧いた直後は洞窟の口でこの間だけ点滅する。動かず、触れても当たらない。
   いきなり実体で出てくると出会い頭の事故になるため。 */
/* よろけ。つるはしで敵を叩くと潰れて動けなくなり、**触れてもミスにならない**。
   叩くほど深く潰れ、時間で1段ずつ戻る。戻っていく姿がそのまま残り時間の表示になる。
   潰れている敵は動かないので、岩の下へ並べてまとめて潰す狙いが成立する。
   ディグダグの「膨らむ」の逆にしてあるのは、真似に見せないため。 */
#define STUN_MAX       3      /* 潰れ具合の段数 */
#define STUN_STEP    120      /* 1段戻るまでのフレーム数 (2秒) */
/* 落盤で盤面の全部の敵をよろけさせる深さ。よろけている敵は触れても無害なので、
   深くすると**全画面が安全な時間**になる。最深(3段)だと6秒 x 炭柱2本 = 12秒で、
   面のクリア時間 15〜30秒の半分近くを占めてしまう。1段(2秒)から試す。
   岩は45フレーム揺れてから沈むので、2秒あれば沈む瞬間には足りている。 */
#define STUN_CAVEIN    1

#define SPAWN_WARM   120      /* 2秒 */

/* 体温オーバーヒートと風呂。常磐炭鉱は地熱約40度＋湧水で過酷 (史実) という設定。
   体温は毎フレーム少しずつ上がり、上限に達すると1ミス。各面に1つある風呂を通過すると
   一瞬で下がる。風呂は敵には壊されないが、落下する岩とダイナマイトの爆風では壊れる。
   壊れても直ちにミスにはならないが、次にオーバーヒートするまでに面をクリアできないと
   1ミスになる (＝冷やす手段が無くなるので自然にそうなる)。 */
#define TEMP_MAX      240     /* 上限 = オーバーヒート */
#define TEMP_TICK     15      /* このフレーム数ごとに体温+1 (満タンまで約60秒) */
/* つるはしを1振りするごとの発熱。時間経過だけだと面が短くなった今、
   風呂へ行く必要がほとんど無い。掘る量そのものを体温に効かせる。 */
#define HEAT_PER_SWING 4
/* 溜め打ち。
     押した瞬間 -> 威力1で即座に1振り (単発の手触りを変えないため)
     押したまま -> **振らずに気合いを溜める**。CHARGE_TIME ごとに1段、最大 CHARGE_MAX
     離した     -> 溜まっていれば、その威力で1振り
     移動した   -> 溜めが切れる
   自動で振り続ける (連射) にすると溜めている感じが出ないので、溜め中は止める。
   **発熱は威力に比例する** (威力3なら+12)。比例させないと「掘りは常に押しっぱなし」が
   最適になり、体温が骨抜きになる。溜めが買っているのは時間であって熱ではない。 */
#define CHARGE_MAX     3
#define CHARGE_TIME   30      /* 1段溜まるまでのフレーム数 (0.5秒) */
/* つるはしが敵に届く升数。1升だけだと、よろけて止まった敵が目の前を塞いだとき、
   その後ろの敵へどう振っても届かず、既に最深の敵へ無駄打ちし続けることになる。 */
#define PICK_REACH     2
/* 風呂は**一瞬で全快させない**。1フレームにこれだけ下がる。
   風呂は出発点の近くの本通りに置かれるので、一瞬で全快だと通りすがりに
   踏むだけで体温が0に戻り、意識して入る必要がまったく無くなる
   (実測: 24秒の間に3回も勝手にリセットされ、体温は上限の13%までしか上がらなかった)。
   240 から冷まし切るには 2秒その場に留まる必要がある。 */
#define BATH_COOL      2
/* 水風呂は **1px でも重なっていれば** 冷える。升にぴったり整列している間だけだと、
   高次の面で通りすがりに掠める操作がまったく報われず、当たっているのか
   バグなのか区別が付かない。 */
#define BATH_BUBBLE    4      /* 泡が1段上がるフレーム数 */
/* 梅干し。倒した敵が落とす。食べると体温が下がる。
   倒す = 時間を買う、という判断を作るためのもの。史実では15〜30分働いたら
   水風呂に入り、梅干しで塩分を補給していた (docs/HISTORY.md §2.1)。 */
#define UME_COOL      60      /* 拾ったときに下がる体温 (TEMP_MAX の 1/4) */

#define FUSE_FRAMES   120     /* ダイナマイトの導火 = 約2秒 */
#define FLASH_FRAMES  8       /* 爆風の閃光 */
#define INVULN_FRAMES 90      /* ミス後の無敵 */
/* ステージクリア = 盤面の黒ダイヤを全部採掘する (coal_left == 0)。
   掘削率はクリア条件ではなく、クリア時の得点ボーナスに使う。 */
#define PTS_DUG_PCT   20      /* クリア時、採掘率1%あたりの得点 */

/* つるはし。黒ダイヤに接触した状態で振ると耐久度を1削る。
   振っている間は動けないので、そこへ敵が寄ってくるのが駆け引きになる。 */
#define PICK_FRAMES   18      /* 1回振り切るまで (0.3秒) */
#define COAL_CRACKED  255     /* coal[] のこの値は「割れて採掘待ち」 */
#define COAL_HP_MAX   8
/* 黒ダイヤは1個ずつばらまかず、**繋がった炭層**として置く。掘って辿り着く距離が
   減り、鉱脈を掘り当てて一息に採る感じが出る。1本の長さは 1..COAL_VEIN_MAX。 */
#define COAL_VEIN_MAX 4
/* 炭柱 (残柱式)。岩の真下に意図的に置く、耐久の高い黒ダイヤ。
   割ると岩の支えが外れて落ちるので、「どれをどの順で割るか」が意味を持つ。
   絵は専用のもの (三ツ枠を巻いた姿)。数字だけの違いでは見分けられなかった。 */
#define PILLAR_HP_ADD  3      /* 通常の黒ダイヤに上乗せする耐久 */
/* 炭柱は岩とは**別の場所**へ置く。落盤で盤面のすべての岩が落ちるので、真上の岩だけ
   特別扱いする理由が無く、「黄枠 = 天井全部が落ちる」という合図のほうが挙動と一致する。
   本数は絞る。炭柱は必ず壊す (クリア条件に数える) ので、本数がそのまま強制的な発熱に
   なる: 面1で1本20、面15で1本32。 */
#define PILLAR_MAX     2
/* 炭柱ごとに「岩が何ブロック沈むか」を持つ。升の左下に数字で出す
   (右上は耐久度)。この沈み込みぶんは **落下距離に数えない**。
   2ブロック沈んだだけでは岩は割れず、沈んだ先からさらに落ちた距離で
   通常どおり判定する (1ブロックなら割れない / 2ブロック以上で割れる)。 */
#define PILLAR_DROP_MAX 3
/* 割れた瞬間に画面を縦へ揺らす (R#18 = SET ADJUST)。 */
#define COAL_SHAKE_FRAMES 10
/* 採掘した瞬間、その升を光らせる。割ったとき (画面揺れ) とは別の演出で、
   「割る」と「手に入れる」を分けて見せる。 */
#define COAL_GET_FRAMES 9
/* 炭層の連鎖採掘。割れた黒ダイヤの升に入ると、繋がっている割れた黒ダイヤを
   すべて同時に採る。「耐久ぶん振って溜める → 一歩踏み込んで一気に放出」という
   溜めと放出になる。
   同時に、割れた黒ダイヤは通れる升なので、炭層を全部割った時点で最大
   COAL_VEIN_MAX 升の直線通路が開通し、**敵の高速道路になる**。
   1個ずつ安全に採るか、全部割ってから一気に採るかがそのままギャンブルになる。 */
#define CHAIN_MAX      8      /* 一度に採れる上限 */
/* 連鎖は**時間で繋がる**。岩が突き抜けて採った黒ダイヤも、自分で歩いて採った
   黒ダイヤも、同じ連鎖として数える。倍率は**採った順**に 200→400→800→…。
   岩は上から下へ抜けるので、その順に倍率が上がっていく。 */
#define CHAIN_WINDOW 180      /* 連鎖が途切れるまでの時間 (3秒) */
#define CHAIN_PTS_MAX 3200    /* 1個あたりの上限 */
/* ヒットストップ。連鎖が伸びた瞬間だけ全部を数フレーム止める。
   **VDP を一切使わない**ので fps を落とさずに手応えだけ増やせる。
   格闘ゲームやアクションで最も効く技法で、殴った感触はここで決まる。 */
#define HITSTOP_BASE   2      /* 1個目で止めるフレーム数 */
#define HITSTOP_MAX    7      /* 連鎖が伸びたときの上限 */
extern uint8_t hitstop;
extern uint8_t combo;         /* 直前の連鎖で採った数。HUD 表示用 */
extern uint8_t combo_t;       /* HUD に出しておく残り時間 */
#define COMBO_SHOW    90      /* 連鎖数を出しておくフレーム数 (1.5秒) */

/* ダイナマイトは投げる武器。向いている方向へ最大5升飛ぶ。
   飛んだ先に敵が居ればそこで爆発 (爆風は1升だけ・地形は掘らない)、
   障害物があれば手前に落ち、何も無ければ5升先に落ちて拾い直せる。 */
#define BOMB_RANGE     5      /* 飛距離 (升) */
#define BOMB_FLY_FRAMES 4     /* 1升進むのに掛けるフレーム数 */
#define LIVES_INIT    3
#define MAX_STAGE     15      /* 全15面。掘り抜きが面番号の形になる */
#define POWDER_PER_STAGE 10   /* 盤面に置く火薬の数 (面によらず一定) */
/* 倒した敵は火薬を落とす。倒しても得点が入るだけだと、資源を消費して敵を倒す
   理由がなく「無視して掘り続けるのが最適」になってしまう。落とすことで
   「1本使って倒し、拾って取り返す」という投資の判断が生まれる。
   岩で潰すと必ず落とし、ダイナマイトだと半々。岩は資源を使わないぶん報われる。 */
#define DROP_BY_ROCK  1       /* 岩で潰した敵は必ず落とす */

/* --- パレット番号 --- */
#define COL_TUNNEL 1
#define COL_SKIN   2
#define COL_DARK   3
#define COL_DIRT   6
#define COL_HUD    4
#define COL_BODY   7
#define COL_BLAST  11
#define COL_WHITE  15
#define COL_RED    8
#define COL_FUSE   10
#define COL_ROCK   14
#define COL_RAT    9
#define COL_BAT    13
#define COL_BATH   5
#define COL_DIA    12

/* --- 方向 --- */
#define DIR_NONE  0
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* --- スプライト面 --- */
#define SPR_PLAYER 0
#define SPR_BOMB   1
#define SPR_ROCK   2                        /* 面 2..5 */
#define SPR_ENEMY  (SPR_ROCK + ROCK_MAX)    /* 面 6..9 */
#define SPR_DEBRIS (SPR_ENEMY + ENEMY_MAX)  /* 石炭のかけら */
#define SPR_ACTIVE (SPR_DEBRIS + DEBRIS_MAX) /* 実際に使う面の数。OAM 転送量を抑える */

/* スプライトの絵番号 */
#define PAT_MINER 0
#define PAT_BOMB  1
#define PAT_ROCK  2
#define PAT_RAT   3
#define PAT_BAT   4
#define PAT_SPLIT 5      /* 割れた岩。消える前に1秒だけ出す */
#define PAT_STUN1 6      /* よろけ 少し */
#define PAT_STUN2 7      /* よろけ もっと */
#define PAT_STUN3 8      /* よろけ かなり */
#define PAT_BRACE 9      /* 溜めの構え・下向き (腰を落として踏ん張る) */
/* 自機の絵。向きと動作でここから選ぶ。
   正面 (PAT_MINER) は**面の開始時、まだ一度も動いていない間だけ**出す。
   以後は必ず上下左右のどれかを向いている。 */
#define PAT_WD1  10      /* 歩き 下 1 */
#define PAT_WD2  11
#define PAT_WU1  12      /* 歩き 上 1 */
#define PAT_WU2  13
#define PAT_WL1  14      /* 歩き 左 1 */
#define PAT_WL2  15
#define PAT_WR1  16      /* 歩き 右 1 */
#define PAT_WR2  17
#define PAT_BRU  18      /* 溜めの構え 上 */
#define PAT_BRL  19      /* 溜めの構え 左 */
#define PAT_BRR  20      /* 溜めの構え 右 */
#define PAT_STD  21      /* 振り下ろし 下 */
#define PAT_STU  22
#define PAT_STL  23
#define PAT_STR  24
#define PAT_CHIP 25      /* 石炭のかけら。採った升から自機へ吸い寄せられる */
/* かけらは「岩でも採掘できる」を絵で説明するためのもの。言葉にしにくい仕組みなので、
   自機へ集まる動きで「いま手に入った」を伝える。 */
#define DEBRIS_MAX     6
#define DEBRIS_FRAMES 34      /* 出しておく時間。短いと一瞬で終わって見えない */
#define DEBRIS_POP     8      /* 最初に外へ弾けるフレーム数 */
void    debris_clear(void);
void    debris_spawn(uint8_t cx, uint8_t cy);
void    debris_tick(void);
void    debris_draw(void);
#define WALK_FRAMES 8    /* 歩きの絵を切り替えるフレーム数 */
/* 向いている方向で自機の絵を選ぶ。moved=一度でも動いたか、swinging=振っている最中、
   bracing=溜めている、walk=歩きのコマ (0/1)。 */
uint8_t player_pattern(void);

/* --- field.c --- */
extern uint8_t  dug[NCELL];      /* 掘り済みフラグ */
extern uint8_t  powder[NCELL];   /* 火薬 (ダイナマイトの弾薬) */
extern uint8_t  bombdrop[NCELL]; /* 投げて落ちたダイナマイト。拾い直せる */
extern uint8_t  ume[NCELL];      /* 梅干し。倒した敵が落とす。拾うと体温が下がる */
extern uint8_t  pillar[NCELL];   /* 0 = 通常 / 1以上 = 炭柱。値は壊したとき岩が沈むブロック数 */
/* 黒ダイヤ (石炭)。土と違って自機の掘りでは進めず、障害物として立ちはだかる。
   つるはしで1回叩くごとに耐久度が1減り、0になると COAL_CRACKED (割れた状態) になる。
   割れると通れるようになり、その升に入ると採掘できる。
   値: 0=無し / 1..COAL_HP_MAX=残り耐久度 / COAL_CRACKED=割れて採掘待ち */
extern uint8_t  coal[NCELL];
extern uint8_t  coal_left;       /* まだ採掘していない黒ダイヤの数。0 でステージクリア */
/* 升と升の境目が抜けているか (薄皮一枚の土の膜が残っていないか)。
   掘り跡は内側2pxだけを抜くので、**隣り合う升が両方 dug でも間に土の膜が残る**
   ことがある。dug だけで判定すると、敵がその膜を実体のまますり抜けてしまう。
   bit0 = 右隣との境目が抜けている / bit1 = 真下との境目が抜けている */
extern uint8_t  cell_link[NCELL];
extern uint16_t dug_n;           /* 掘ったセル数 */
extern uint16_t dug_n0;          /* 面の開始時点の掘済み数 (切羽圧の基準) */
extern uint8_t  dug_pct;         /* 採掘率 % */
extern uint8_t  stage;
extern uint8_t  start_c, start_r;
extern uint8_t  nest_c, nest_r;   /* 敵が湧く巣 = 画面中央の既掘セル */
extern uint8_t  bath_c, bath_r;   /* 風呂 = 自機の出現位置の近く (面ごとに違う) */
extern uint8_t  bath_on;          /* 風呂が健在か */

uint8_t cell_dug(int8_t cx, int8_t cy);
#define LINK_RIGHT 1
#define LINK_DOWN  2
uint8_t cell_linked(int8_t cx, int8_t cy, uint8_t dir);   /* その向きへ膜なしで通れるか */
void    link_carve(uint8_t px_, uint8_t py_);
void    link_open(uint8_t cx, uint8_t cy);   /* 割れた黒ダイヤの升を通路として繋ぐ */             /* 自機が膜を抜いた分を記録する */
void    dig_sweep(uint8_t cx, uint8_t cy);
void    draw_dug_cell(int8_t cx, int8_t cy);
void    draw_cell_dirt(int8_t cx, int8_t cy);
void    draw_powder(uint8_t cx, uint8_t cy);
void    draw_bombdrop(uint8_t cx, uint8_t cy);
/* 倒した敵の落とし物。is_ume なら梅干し (冷却)、そうでなければ火薬。 */
void    drop_item_at(int8_t cx, int8_t cy, uint8_t is_ume);
void    draw_ume(uint8_t cx, uint8_t cy);
void    coal_crush_at(int8_t cx, int8_t cy);   /* 落ちてきた岩が割れた黒ダイヤを砕く */
/* 落盤で、岩が載っている黒ダイヤを割る。つるはしで割ったのと同じ状態にするが、
   炭柱だった場合も落盤を再帰させない。 */
void    coal_crack_by_rock(int8_t cx, int8_t cy);
/* 落ちてきた岩が黒ダイヤを突き抜ける。割れた状態を経由せず、そのまま採取する。 */
uint8_t coal_smash(int8_t cx, int8_t cy);   /* 採ったら1を返す */
void    draw_bath(uint8_t cx, uint8_t cy);
/* 冷えている最中の水風呂。泡を1つ描いて、効いていることを見せる。 */
void    draw_bath_active(uint8_t phase);
extern uint8_t bathing;       /* 1 = いま水風呂に触れて冷えている */
void    draw_coal(uint8_t cx, uint8_t cy);       /* 耐久度に応じて絵を選ぶ */
void    draw_bonus(uint8_t cx, uint8_t cy, uint8_t kind);
void    draw_cave(uint8_t cx, uint8_t cy);
uint8_t cell_coal(int8_t cx, int8_t cy);         /* 立ちはだかる黒ダイヤなら1 (割れたものは0) */
uint8_t coal_hit(int8_t cx, int8_t cy);          /* つるはし1回。割れたら1を返す */
void    coal_take(uint8_t cx, uint8_t cy);       /* 割れた黒ダイヤを採掘する */
void    field_redraw_cell(int8_t cx, int8_t cy);
void    build_stage(void);
extern const uint16_t stage_seed[15];   /* 面ごとの盤面の種 (0=既定式) */
extern uint16_t layout_seed_force;      /* 0 以外なら強制的にこの種を使う (道具用) */
uint16_t layout_seed(void);
void    set_dirt_palette(void);
void    cavein_flash(void);        /* 落盤の閃光を始める */
void    cavein_flash_tick(void);   /* 毎フレーム呼ぶ。終わったら色を戻す */   /* 面ごとの土の色 */
void    coal_shake_tick(void);
void    field_shake(uint8_t frames);   /* 画面を揺らす (岩の着地など) */
/* 面をまたぐ前に演出の状態を全部落とす。**やらないと画面がずれたまま次の面が始まる。**
   揺れ (R#18) は面を組み直しても戻らないので、明示的に中央へ戻す必要がある。 */
void    field_fx_reset(void);
void    popup_clear(void);
/* 面の開始に出す漢字。BIOS フォントに漢字が無いので5文字だけ自前で持つ。 */
#define KJ_DAI   0      /* 第 */
#define KJ_KAI   1      /* 階 */
#define KJ_SOU   2      /* 層 */
#define KJ_HIRA  3      /* 開 */
#define KJ_SHI   4      /* 始 */
#define KJ_NUM   5      /* ここから 一二三四五六七八九十。KJ_NUM + (n-1) で引ける */
#define KJ_TEN  14      /* 十 */
#define KJ_N    15      /* 字数。**tools/mkkanji.py の order と一致させること** */
extern const uint8_t kanji_bits[KJ_N][32];
void    kanji_draw(uint16_t x, uint16_t y, uint8_t idx, uint8_t fg, uint8_t bg);
/* 溜め中の汗。**スプライトではなく背景に描く。** 自機のスプライトは16x16が
   ほぼ埋まっていて1行1色なので汗を足す余地が無く、面を増やすと横一列8枚の
   制限に触れる。溜め中は必ず静止しているので、背景に描いて離したら消せる。 */
/* 溜め中の見た目。汗＋振りかぶったつるはしを、自機の升へ背景で描く。 */
void    draw_charge_fx(uint8_t cx, uint8_t cy, uint8_t level);
/* 振り下ろしの軌跡。振っている間、つるはしが上から的の側へ降りていく。 */
void    draw_swing_fx(uint8_t cx, uint8_t cy, uint8_t dir, uint8_t phase);
void    clear_sweat(void);
extern uint8_t swing_pow;   /* いまの威力 (0 = 溜めていない) */
void    link_smash(uint8_t cx, uint8_t cy);  /* 岩が縦に突き破った膜を開く */   /* 割れた瞬間の画面揺れと、採掘した升の閃き。毎フレーム呼ぶ */

/* --- player.c --- */
extern uint8_t px, py;
extern uint8_t lives;
extern uint8_t dead;
extern uint8_t dead_cause;    /* 0=接触や潰し 1=オーバーヒート */
#define DEAD_HEAT 1
extern uint8_t invuln;
extern uint8_t temp;          /* 体温 0..TEMP_MAX */

void player_start(void);
void player_update(void);
void player_respawn(void);
void player_carve(void);
void player_pick(uint8_t held);  /* つるはしを振る。held = A ボタンを押しているか */
uint8_t player_dir(void);       /* いま向いている方向 */
uint8_t player_cell_c(void);
uint8_t player_cell_r(void);

/* --- rock.c --- */
#define ROCK_NONE 0
#define ROCK_WAIT 1
#define ROCK_JIG  2
#define ROCK_FALL 3
#define ROCK_SPLIT 4          /* 割れた姿を見せている最中。当たり判定も支えも無い */
/* 割れた岩を出しておくフレーム数。即座に消すと、何が起きたのか分からない。 */
#define ROCK_SPLIT_FRAMES 60
#define ROCK_LAND_SHAKE   6   /* 着地の軽い揺れ */
/* 落盤の閃光。土のパレットを白へ飛ばして戻す。**VRAM を一切触らずに画面全体が光る**ので、
   盤面全体が動く事件を盤面全体の変化で伝えられる。土の色は面ごとに変わる (茶/灰/青) が、
   白飛ばしならどの色からでも効く。 */
#define FLASH_CAVEIN  6       /* 光らせるフレーム数 */

extern uint8_t rock_state[ROCK_MAX];
extern uint8_t rock_x[ROCK_MAX], rock_y[ROCK_MAX];

void    rock_clear(void);
void    rock_place(uint8_t c, uint8_t r);
int8_t  rock_at(int8_t cx, int8_t cy);          /* そのセルの岩の番号 / 無ければ -1 */
uint8_t rock_can_push(int8_t i, int8_t dx, uint8_t into_dirt);    /* 押せるか調べるだけ */
uint8_t rock_push(int8_t i, int8_t dx, uint8_t into_dirt);   /* 1ドットだけ動かす。1=押せた */
/* (x,y) の者が dx へ進むとき押すことになる岩。無ければ -1。升ではなくドットで見る。 */
int8_t  rock_ahead_of(uint8_t x, uint8_t y, int8_t dx);
uint8_t rock_crush_check(uint8_t cx, uint8_t cy);
void    rock_destroy_at(int8_t cx, int8_t cy);  /* 爆風で吹き飛ばす */
void    rock_update(void);
/* 炭柱が壊れた = 天盤が落ちる。盤面のすべての岩が揺れてから blocks 升沈む。 */
void    rock_collapse(uint8_t blocks);
/* 揺れ・落下・破砕表示のいずれかが進行中の岩があるか。クリアを待たせるのに使う。 */
uint8_t rock_busy(void);
void    rock_draw(void);

/* --- enemy.c --- */
extern uint8_t en_state[ENEMY_MAX];
extern uint8_t en_x[ENEMY_MAX], en_y[ENEMY_MAX];
extern uint8_t en_kind[ENEMY_MAX];   /* EN_RED / EN_PINK / EN_CYAN / EN_ORANGE */
extern uint16_t en_rest[ENEMY_MAX];  /* 倒された敵が洞窟にこもる残りフレーム */

uint8_t rnd8(void);
void    rnd_seed(uint16_t s);
uint16_t rnd_get(void);          /* いまの乱数の状態 */
void    enemy_clear(void);
void    enemy_stage_reset(void);      /* 面開始時に出現数と湧きタイマを仕込む */
void    enemy_update(void);
void    enemy_draw(void);
extern uint16_t en_morph[ENEMY_MAX];   /* 透明化までの残りフレーム (計測用) */
extern uint8_t  en_ghost[ENEMY_MAX];   /* 1 = 透明化中 (計測用) */
extern uint8_t  en_warm[ENEMY_MAX];    /* 1 = 湧いて点滅中でまだ当たらない (計測用) */
extern uint8_t  en_stun[ENEMY_MAX];    /* 0 = 通常 / 1..STUN_MAX = よろけ (深いほど長い) */
/* 敵をよろけさせる。倒した数ではなく、当たった敵が居たかを返す。 */
uint8_t enemy_stun_at(int8_t cx, int8_t cy, int8_t dx, int8_t dy, uint8_t power);
/* 落盤。盤面の敵をまとめてよろけさせる (位置合わせが要らないので命中率が上がる)。 */
void    enemy_stun_all(uint8_t power);
/* 岩に押された敵を1升下がらせる。doit=0 なら可否を見るだけ。1=下がれる 0=下がれない */
uint8_t enemy_shove(int8_t cx, int8_t cy, int8_t dx, uint8_t doit);
uint8_t enemy_kill_at(int8_t cx, int8_t cy, uint8_t by_rock);  /* 倒した数を返す */
uint8_t enemy_at(int8_t cx, int8_t cy);        /* その升に敵が居るか (投げたダイナマイトの判定) */

/* --- bomb.c --- */
extern uint8_t stock;        /* ダイナマイトのストック (次ステージへ持ち越し) */
extern uint8_t bomb_state;   /* 0=無 1=飛行中 2=爆風閃光中 */
extern uint8_t bomb_c, bomb_r;

void bomb_clear(void);
void bomb_throw(uint8_t dir);   /* 向いている方向へ投げる */
void bomb_update(void);

extern uint16_t frame_n;      /* 実フレーム数 (速度計測用) */

/* --- title.c --- */
void title_draw(void);

/* --- bonus.c --- */
/* 岩を2個割ると一定時間だけ出現し、上を通過すると取れる。効果は3種類。 */
#define ROCKS_PER_BONUS 2
#define BONUS_FRAMES  600      /* 出ている時間 (10秒) */
#define BONUS_SCORE 0
#define BONUS_COOL  1
#define BONUS_LIFE  2

extern uint8_t bonus_on;
extern uint8_t bonus_c, bonus_r;
extern uint8_t bonus_kind;

void bonus_stage_reset(void);
void bonus_rock_broken(void);
void bonus_update(void);
void bonus_take(void);

/* --- hud.c --- */
#define SCORE_DIGITS 6
extern uint8_t score_d[SCORE_DIGITS];   /* 10進の桁 (0 が最上位) */

void hud_reset(void);
void hud_draw(void);
void score_reset(void);
void score_add(uint16_t v);
void score_kills(uint8_t n, uint8_t by_rock);   /* 同時に倒した数で倍々 */
uint16_t kill_value(uint8_t n, uint8_t by_rock); /* その点数だけを返す (表示用) */
/* 倒した升へ得点を出す (ディグダグ方式)。何点入ったかがその場で分かる。 */
void score_popup(uint8_t cx, uint8_t cy, uint16_t v);
void popup_tick(void);
#define POPUP_FRAMES 60       /* 出しておく時間 (1秒) */

/* 得点表 */
#define PTS_POWDER  10
#define PTS_COAL   200            /* 黒ダイヤ1個を採掘 */
#define PTS_COAL_CRACK 50         /* 割った瞬間 */
#define PTS_DIG      5
#define PTS_BONUS 1000
#define PTS_LEFTOVER_DYN 100            /* 面クリア時に残ダイナマイト1本あたり */

#endif
