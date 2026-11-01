/* 面エディタの中身。
 *
 * **採点の規則はここに持たない。** metrics.py / emotion.py を呼んで、
 * 返ってきた合格条件 (gates) と値をそのまま並べるだけ。エディタ側に同じ
 * しきい値を書き写すと、片方を直したときに静かに食い違う。
 *
 * **盤面の整合もここで判定しない。** sim に読ませて、文句をそのまま見せる
 * (docs/STAGE_FORMAT.md の検査が唯一の正)。
 *
 * 見た目は本物の tiles.png / sprites.png をそのまま貼っている。土と坑道だけは
 * 絵ではなく色で塗る (ゲーム側も vdp_fill + draw_grain でそうしている)。
 * **粒の散らし方は本物と違う**ので、土かどうかを見分ける目安と思うこと。
 */
'use strict';

const COLS = 16, ROWS = 11, CELL = 16, Z = 3;      /* Z = 画面での拡大率 */
const HP_CHARS = '123456789AB';                    /* 耐久 1..11 を1文字で */
const COAL_HP_MAX = 11, PILLAR_HP_ADD = 3, GAS_MAX = 4, ROCK_MAX = 8;

/* tiles.png の並び (Makefile の ART_TILE_NAMES) */
const T = { POWDER: 0, BATH: 1, CAVE: 5, DIA: 6, DIACRK: 8, PILLAR: 9,
            SPRING: 10, TOIR: 12, TOIL: 13, RAIL: 16, RAILL: 17, RAILR: 18, GAS: 19 };
/* sprites.png の並び (ART_SPR_NAMES) */
const SP = { MINER: 0, ROCK: 2, ROCKPROP: 28 };

/* 土の粒。**本物 (draw_grain) は散らす。**ここは見分けるための目安。 */
const GRAIN = [[2, 3], [9, 5], [5, 11], [13, 9], [7, 1], [11, 14], [3, 8], [14, 2]];

/* **合図は URL の断片 (#t=...) から取る。** 問い合わせ (?t=...) だと Referer や
   ブラウザの履歴に残る。断片はサーバへ送られないので、ここで読んで毎回添える。
   古い形の URL を開いた人のために、問い合わせ側も見る。 */
const TOKEN = new URLSearchParams(location.hash.slice(1)).get('t')
           || new URLSearchParams(location.search).get('t') || '';

const $ = (id) => document.getElementById(id);

async function api(path, opt) {
  const sep = path.includes('?') ? '&' : '?';
  const r = await fetch(path + sep + 't=' + encodeURIComponent(TOKEN), opt);
  return r.json();
}

/* ------------------------------------------------------------------ *
 * 状態。**面データの形そのまま持つ** (docs/STAGE_FORMAT.md)。
 * 升の中身を1文字で持てば、保存はつなげるだけで済む。
 * ------------------------------------------------------------------ */
const ST = {
  stage: 1, loop: 0,
  dug: null, cell: null, over: null,
  start: [7, 1], nest: [8, 5], bath: null, spring: null, cart: null,
  gas: [], rockOrder: null, veins: null,
};
let hp = 3;                      /* 黒ダイヤの筆が置く耐久 */
let DIRT = { 1: [[4, 2, 1], [3, 1, 1]] };   /* /api/palette で上書きする */

const grid = (ch) => Array.from({ length: ROWS }, () => Array(COLS).fill(ch));

function blank() {
  ST.dug = grid('.'); ST.cell = grid(' '); ST.over = grid(' ');
  ST.start = [7, 1]; ST.nest = [8, 5];
  ST.bath = null; ST.spring = null; ST.cart = null; ST.gas = [];
  ST.rockOrder = null; ST.veins = null;
  /* 出発点と巣は坑道でなければならない */
  ST.dug[1][7] = ' '; ST.dug[5][8] = ' ';
}

const isHp = (ch) => HP_CHARS.indexOf(ch) >= 0;
const hpOf = (ch) => HP_CHARS.indexOf(ch) + 1;
const isCoal = (ch) => isHp(ch) || ch === 'x';
const act = () => Math.min(5, Math.floor((ST.stage - 1) / 3) + 1);
const msx = (v) => `rgb(${Math.round(v[0] * 255 / 7)},${Math.round(v[1] * 255 / 7)},${Math.round(v[2] * 255 / 7)})`;
const at = (p, c, r) => p && p[0] === c && p[1] === r;

/* ---- 元に戻す ---- */
const undoStack = [];
function snapshot() {
  undoStack.push(JSON.stringify(ST));
  if (undoStack.length > 60) { undoStack.shift(); }
  $('undo').disabled = false;
}
function undo() {
  const s = undoStack.pop();
  if (!s) { return; }
  Object.assign(ST, JSON.parse(s));
  $('undo').disabled = undoStack.length === 0;
  syncHeader(); changed();
}

/* ------------------------------------------------------------------ *
 * 絵
 * ------------------------------------------------------------------ */
const art = { tiles: new Image(), sprites: new Image(), left: 2 };
function loadArt(done) {
  for (const [k, src] of [['tiles', '/art/tiles.png'], ['sprites', '/art/sprites.png']]) {
    art[k].onload = () => { if (--art.left === 0) { done(); } };
    art[k].onerror = () => { if (--art.left === 0) { done(); } };
    art[k].src = src;
  }
}

const cv = $('board'), cx = cv.getContext('2d');

function blit(ctx, img, idx, c, r, z) {
  ctx.drawImage(img, idx * CELL, 0, CELL, CELL, c * CELL * z, r * CELL * z, CELL * z, CELL * z);
}
const tile = (i, c, r) => blit(cx, art.tiles, i, c, r, Z);
const spr = (i, c, r) => blit(cx, art.sprites, i, c, r, Z);

/* 地。掘ってあれば坑道 (黒)、土なら幕ごとの2色。 */
function ground(ctx, dug, c, r, z) {
  const x = c * CELL * z, y = r * CELL * z, w = CELL * z;
  if (dug) {
    ctx.fillStyle = '#000';
    ctx.fillRect(x, y, w, w);
    return;
  }
  const [d1, d2] = DIRT[act()] || DIRT[1];
  ctx.fillStyle = msx(d1); ctx.fillRect(x, y, w, w);
  ctx.fillStyle = msx(d2);
  for (const [gx, gy] of GRAIN) { ctx.fillRect(x + gx * z, y + gy * z, z, z); }
}

/* 溜まり湯。**水面の高さは本物に合わせてある** (WATER_LEVEL = 7px)。 */
function water(c, r) {
  const x = c * CELL * Z, y = r * CELL * Z;
  cx.fillStyle = msx([2, 5, 7]);
  cx.fillRect(x, y + 7 * Z, CELL * Z, (CELL - 7) * Z);
  cx.fillStyle = msx([5, 6, 7]);
  cx.fillRect(x, y + 7 * Z, CELL * Z, Z);
}

/* 耐久の数。**升の上辺に出す** (本物の draw_coal も2桁を上辺に彫る)。
   色ではなく数で読ませる。 */
function hpNum(n, c, r) {
  const x = c * CELL * Z, y = r * CELL * Z, s = String(n);
  const w = (s.length === 1 ? 7 : 12) * Z;
  cx.fillStyle = 'rgba(0,0,0,.72)';
  cx.fillRect(x + Z, y + Z, w, 8 * Z);
  cx.fillStyle = '#fff';
  cx.font = `bold ${7 * Z}px ui-monospace, Menlo, monospace`;
  cx.textBaseline = 'top';
  cx.fillText(s, x + 2 * Z, y + 1.4 * Z);
}

function draw() {
  cx.imageSmoothingEnabled = false;

  for (let r = 0; r < ROWS; r++) {
    for (let c = 0; c < COLS; c++) { ground(cx, ST.dug[r][c] === ' ', c, r, Z); }
  }
  /* レールは中身より先 (ゲーム側も坑道を描いた直後に敷く) */
  if (ST.cart) {
    for (let c = ST.cart.from; c <= ST.cart.to; c++) {
      tile(c === ST.cart.from ? T.RAILL : c === ST.cart.to ? T.RAILR : T.RAIL, c, ST.cart.row);
    }
  }
  for (let r = 0; r < ROWS; r++) {
    for (let c = 0; c < COLS; c++) {
      const k = ST.cell[r][c], o = ST.over[r][c];
      if (k === 'O')      { spr(SP.ROCK, c, r); }
      else if (k === 'o') { spr(SP.ROCKPROP, c, r); }
      else if (k === 'x') { tile(T.DIACRK, c, r); }
      else if (k === '>') { tile(T.TOIR, c, r); }
      else if (k === '<') { tile(T.TOIL, c, r); }
      else if (isHp(k)) {
        /* **炭柱は黒ダイヤの絵ではなく炭柱の絵。** ゲーム側も枠を巻いて描く。 */
        tile(o === '*' ? T.PILLAR : T.DIA, c, r);
        hpNum(hpOf(k), c, r);
      }
      if (o === 'p')      { tile(T.POWDER, c, r); }
      else if (o === '~') { water(c, r); }
    }
  }
  /* 一点物は最後。坑道の上に乗る。 */
  if (ST.nest)   { tile(T.CAVE, ST.nest[0], ST.nest[1]); }
  if (ST.bath)   { tile(T.BATH, ST.bath[0], ST.bath[1]); }
  if (ST.spring) { tile(T.SPRING, ST.spring[0], ST.spring[1]); }
  for (const [c, r] of ST.gas) { tile(T.GAS, c, r); }
  if (ST.start)  { spr(SP.MINER, ST.start[0], ST.start[1]); }

  if ($('gridon').checked) {
    cx.strokeStyle = 'rgba(240,230,204,.18)';
    cx.lineWidth = 1;
    for (let c = 1; c < COLS; c++) {
      cx.beginPath(); cx.moveTo(c * CELL * Z, 0); cx.lineTo(c * CELL * Z, ROWS * CELL * Z); cx.stroke();
    }
    for (let r = 1; r < ROWS; r++) {
      cx.beginPath(); cx.moveTo(0, r * CELL * Z); cx.lineTo(COLS * CELL * Z, r * CELL * Z); cx.stroke();
    }
  }
}

/* ------------------------------------------------------------------ *
 * 置くもの
 * ------------------------------------------------------------------ */
const BRUSHES = [
  { id: 'dig',    grp: '掘る',   label: '坑道 (掘る)' },
  { id: 'fill',   grp: '掘る',   label: '土 (埋める)' },

  { id: 'coal',   grp: '升の中身', label: '黒ダイヤ' },
  { id: 'crack',  grp: '升の中身', label: '割れた黒ダイヤ' },
  { id: 'rock',   grp: '升の中身', label: '岩' },
  { id: 'prop',   grp: '升の中身', label: '岩 (支柱つき)' },
  { id: 'toir',   grp: '升の中身', label: '樋 右下がり' },
  { id: 'toil',   grp: '升の中身', label: '樋 左下がり' },
  { id: 'cclr',   grp: '升の中身', label: '中身を消す' },

  { id: 'pillar', grp: '上に乗るもの', label: '炭柱' },
  { id: 'powder', grp: '上に乗るもの', label: '火薬' },
  { id: 'pool',   grp: '上に乗るもの', label: '溜まり湯' },
  { id: 'oclr',   grp: '上に乗るもの', label: '上のものを消す' },

  { id: 'start',  grp: '一点物', label: '出発点' },
  { id: 'nest',   grp: '一点物', label: '敵の巣' },
  { id: 'bath',   grp: '一点物', label: '水風呂' },
  { id: 'spring', grp: '一点物', label: '湧き出し口' },
  { id: 'gas',    grp: '一点物', label: `ガス (最大${GAS_MAX})` },
  { id: 'rail',   grp: '一点物', label: 'レール (横へ引く)' },
];
let brush = 'dig';

/* **引きずらないもの。** 一点物とガスは、通った升ぜんぶに置いたら困る。 */
const ONE_OFF = new Set(['start', 'nest', 'bath', 'spring', 'gas']);

/* 見本で土を敷くもの (黒ダイヤ・岩・炭柱は土の中にある) */
const ON_DIRT = new Set(['fill', 'coal', 'rock', 'prop', 'pillar']);

/* 一覧に出す見本。盤面と同じ絵を 16x16 で1枚だけ描く。 */
function preview(ctx, id) {
  ground(ctx, !ON_DIRT.has(id), 0, 0, 1);
  const t = (i) => blit(ctx, art.tiles, i, 0, 0, 1);
  const s = (i) => blit(ctx, art.sprites, i, 0, 0, 1);
  switch (id) {
    case 'coal':   t(T.DIA); break;
    case 'crack':  t(T.DIACRK); break;
    case 'rock':   s(SP.ROCK); break;
    case 'prop':   s(SP.ROCKPROP); break;
    case 'toir':   t(T.TOIR); break;
    case 'toil':   t(T.TOIL); break;
    case 'pillar': t(T.PILLAR); break;
    case 'powder': t(T.POWDER); break;
    case 'pool':
      ctx.fillStyle = msx([2, 5, 7]); ctx.fillRect(0, 7, CELL, CELL - 7);
      ctx.fillStyle = msx([5, 6, 7]); ctx.fillRect(0, 7, CELL, 1); break;
    case 'start':  s(SP.MINER); break;
    case 'nest':   t(T.CAVE); break;
    case 'bath':   t(T.BATH); break;
    case 'spring': t(T.SPRING); break;
    case 'gas':    t(T.GAS); break;
    case 'rail':   t(T.RAIL); break;
    case 'cclr': case 'oclr':
      ctx.strokeStyle = 'rgba(240,230,204,.7)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(4, 4); ctx.lineTo(12, 12);
      ctx.moveTo(12, 4); ctx.lineTo(4, 12); ctx.stroke(); break;
    default: break;
  }
}

function buildBrushes() {
  const box = $('brushes');
  box.textContent = '';
  let grp = null;
  for (const b of BRUSHES) {
    if (b.grp !== grp) {
      grp = b.grp;
      const h = document.createElement('h2');
      h.textContent = grp;
      box.appendChild(h);
    }
    const el = document.createElement('button');
    el.className = 'brush' + (b.id === brush ? ' on' : '');
    el.dataset.id = b.id;
    el.title = b.label;
    el.setAttribute('aria-pressed', b.id === brush ? 'true' : 'false');
    const mark = document.createElement('span');
    mark.className = 'mark';
    mark.textContent = b.id === brush ? '▸' : '';
    const pc = document.createElement('canvas');
    pc.width = pc.height = CELL;
    preview(pc.getContext('2d'), b.id);
    const tx = document.createElement('span');
    tx.textContent = b.id === 'coal' ? `${b.label} 耐久${hp}` : b.label;
    el.append(mark, pc, tx);
    el.onclick = () => { brush = b.id; buildBrushes(); };
    box.appendChild(el);
  }
}

/* ------------------------------------------------------------------ *
 * 盤面を触る
 * ------------------------------------------------------------------ */
function setGas(c, r, erase) {
  const k = ST.gas.findIndex((g) => g[0] === c && g[1] === r);
  if (erase || k >= 0) {
    if (k >= 0) { ST.gas.splice(k, 1); }
    return;
  }
  if (ST.gas.length >= GAS_MAX) {
    say(`ガスは ${GAS_MAX} 個まで`);
    return;
  }
  ST.dug[r][c] = ' ';
  ST.gas.push([c, r]);
}

function paint(c, r, erase) {
  const put = (k) => { ST.cell[r][c] = erase ? ' ' : k; };
  switch (brush) {
    case 'dig':
    case 'fill': {
      /* 「掘る」の右押しは埋める。「埋める」の右押しは掘る。 */
      const toDug = (brush === 'dig') !== erase;
      ST.dug[r][c] = toDug ? ' ' : '.';
      /* **火薬と溜まり湯は坑道のものなので、埋めたら一緒に外す。**
         炭柱は土の中のものなので残す。 */
      if (!toDug && (ST.over[r][c] === 'p' || ST.over[r][c] === '~')) {
        ST.over[r][c] = ' ';
      }
      break;
    }

    case 'coal':  put(HP_CHARS[hp - 1]); if (!erase) { ST.dug[r][c] = '.'; } break;
    case 'crack': put('x');              if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'rock':  put('O'); break;
    case 'prop':  put('o'); break;
    case 'toir':  put('>'); if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'toil':  put('<'); if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'cclr':  ST.cell[r][c] = ' '; break;

    case 'pillar':
      if (erase) { ST.over[r][c] = ' '; break; }
      /* **炭柱は黒ダイヤの升にだけ置ける。** 空の升なら黒ダイヤも一緒に置く
         (耐久は通常 + PILLAR_HP_ADD。ゲーム側の place_pillars と同じ)。 */
      if (!isHp(ST.cell[r][c])) {
        ST.cell[r][c] = HP_CHARS[Math.min(COAL_HP_MAX, hp + PILLAR_HP_ADD) - 1];
        ST.dug[r][c] = '.';
      }
      ST.over[r][c] = '*';
      break;
    case 'powder': ST.over[r][c] = erase ? ' ' : 'p'; if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'pool':   ST.over[r][c] = erase ? ' ' : '~'; if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'oclr':   ST.over[r][c] = ' '; break;

    case 'start':
      if (erase) { say('出発点は外せない'); break; }
      ST.start = [c, r]; ST.dug[r][c] = ' '; break;
    case 'nest':
      if (erase) { say('敵の巣は外せない'); break; }
      ST.nest = [c, r]; ST.dug[r][c] = ' '; break;
    case 'bath':
      ST.bath = erase ? null : [c, r]; if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'spring':
      ST.spring = erase ? null : [c, r]; if (!erase) { ST.dug[r][c] = ' '; } break;
    case 'gas': setGas(c, r, erase); break;
    default: break;
  }
}

let stroke = null;          /* 引いている最中 { erase, rail } */

function cellAt(e) {
  const b = cv.getBoundingClientRect();
  const c = Math.floor((e.clientX - b.left) / (b.width / COLS));
  const r = Math.floor((e.clientY - b.top) / (b.height / ROWS));
  return (c >= 0 && c < COLS && r >= 0 && r < ROWS) ? [c, r] : null;
}

cv.addEventListener('contextmenu', (e) => e.preventDefault());

cv.addEventListener('pointerdown', (e) => {
  const p = cellAt(e);
  if (!p) { return; }
  cv.setPointerCapture(e.pointerId);
  snapshot();
  const erase = e.button === 2;
  if (brush === 'rail') {
    if (erase) { ST.cart = null; stroke = { erase, rail: null }; changed(); return; }
    stroke = { erase, rail: { row: p[1], from: p[0] } };
    ST.cart = { row: p[1], from: p[0], to: p[0] };
    ST.dug[p[1]][p[0]] = ' ';
  } else {
    stroke = { erase, rail: null };
    paint(p[0], p[1], erase);
  }
  changed();
});

cv.addEventListener('pointermove', (e) => {
  const p = cellAt(e);
  $('hint').textContent = p
    ? `列${p[0]} 行${p[1]}  —  左の一覧から選んで、押すか引く。右押しで消す`
    : '左の一覧から選んで、盤面を押すか引く。右押しで消す。';
  if (!stroke || !p) { return; }
  if (stroke.rail) {
    const a = Math.min(stroke.rail.from, p[0]), b = Math.max(stroke.rail.from, p[0]);
    ST.cart = { row: stroke.rail.row, from: a, to: b };
    for (let c = a; c <= b; c++) { ST.dug[stroke.rail.row][c] = ' '; }
  } else if (!ONE_OFF.has(brush)) {
    paint(p[0], p[1], stroke.erase);   /* 一点物とガスは引きずらない */
  }
  changed();
});

addEventListener('pointerup', () => { stroke = null; });

addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') { return; }
  if ((e.metaKey || e.ctrlKey) && e.key === 'z') { e.preventDefault(); undo(); return; }
  if (e.key >= '1' && e.key <= '9') { hp = +e.key; buildBrushes(); }
  if (e.key === '0') { hp = 10; buildBrushes(); }
  if (e.key === '-') { hp = 11; buildBrushes(); }
});

/* ------------------------------------------------------------------ *
 * 面データの読み書き
 * ------------------------------------------------------------------ */
function rockCells() {
  const out = [];
  for (let r = 0; r < ROWS; r++) {
    for (let c = 0; c < COLS; c++) {
      if (ST.cell[r][c] === 'O' || ST.cell[r][c] === 'o') { out.push([c, r]); }
    }
  }
  return out;
}
const key = (a) => a.map((p) => p.join(',')).sort().join(' ');

function toFile() {
  const d = {
    format: 'sakiyama-stage', formatVersion: 1,
    generator: { tool: 'tools/stage_editor', note: '手で置いた' },
    stage: ST.stage, loop: ST.loop,
    dug: ST.dug.map((r) => r.join('')),
    cell: ST.cell.map((r) => r.join('')),
    over: ST.over.map((r) => r.join('')),
    start: ST.start, nest: ST.nest,
    bath: ST.bath, spring: ST.spring, cart: ST.cart,
    gas: ST.gas,
  };
  /* **岩の並び順は、指している升が変わっていないあいだだけ持ち越す。**
     変わったら落として、左上から読む順に任せる (そのほうが嘘が無い)。 */
  if (ST.rockOrder && key(ST.rockOrder) === key(rockCells())) { d.rockOrder = ST.rockOrder; }
  /* 仕込んだ鉱脈の覚書も、その升がまだ黒ダイヤのあいだだけ。 */
  if (ST.veins && ST.veins.every(([c, r]) => isCoal(ST.cell[r][c]))) { d.veins = ST.veins; }
  return d;
}

function fromFile(d) {
  const rows = (v, ch) => (Array.isArray(v) && v.length === ROWS)
    ? v.map((s) => String(s).padEnd(COLS, ch).slice(0, COLS).split(''))
    : grid(ch);
  ST.stage = Math.min(15, Math.max(1, +d.stage || 1));
  ST.loop = d.loop ? 1 : 0;
  ST.dug = rows(d.dug, '.');
  ST.cell = rows(d.cell, ' ');
  ST.over = rows(d.over, ' ');
  ST.start = d.start || [7, 1];
  ST.nest = d.nest || [8, 5];
  ST.bath = d.bath || null;
  ST.spring = d.spring || null;
  ST.cart = d.cart || null;
  ST.gas = d.gas || [];
  ST.rockOrder = d.rockOrder || null;
  ST.veins = d.veins || null;
  syncHeader();
}

function syncHeader() {
  $('stage').value = String(ST.stage);
  $('loop').checked = !!ST.loop;
}

/* ------------------------------------------------------------------ *
 * 検査と採点
 * ------------------------------------------------------------------ */
function say(msg) { $('hint').textContent = msg; }

let vTimer = null;
function changed() {
  draw();
  clearTimeout(vTimer);
  vTimer = setTimeout(validate, 350);
}

async function validate() {
  const el = $('valid');
  el.className = 'idle';
  el.textContent = '確かめています…';
  let r;
  try { r = await api('/api/validate', post(toFile())); }
  catch (e) { el.className = 'ng'; el.textContent = String(e); return; }
  if (r.ok) { el.className = 'ok'; el.textContent = 'この面は読み込める'; }
  else      { el.className = 'ng'; el.textContent = r.error || '読み込めない'; }
  return r.ok;
}

const post = (obj) => ({
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(obj),
});

/* 画面に出す数は丸める。emotion.py の表示も丸めている。 */
const round2 = (v) => (typeof v === 'number' && !Number.isInteger(v))
  ? Math.round(v * 100) / 100 : v;

const bar = (v) => {
  const n = Math.max(0, Math.min(10, Math.round(v / 10)));
  return '#'.repeat(n) + '.'.repeat(10 - n);
};

function gateRow(m, g) {
  const [k, lo, hiRaw, label] = g;
  const hi = (hiRaw === null || hiRaw === undefined) ? null : hiRaw;
  const v = m[k];
  const range = (lo !== null && hi !== null) ? `${lo} 〜 ${hi}`
    : (lo !== null) ? `${lo} 以上` : `${hi} 以下`;
  /* **測れなかった項目は判定しない。** metrics.py の verdict() も素通しする
     (ヒヤリが0回なら「爽快率」は分母が無い)。黙って消すと、門が1つ減ったことに
     気づけないので、行は出して「—」と書く。 */
  if (v === null || v === undefined) {
    return { label, v: '—', range, bad: false, none: true };
  }
  let bad = false;
  if (lo !== null && v < lo) { bad = true; }
  if (hi !== null && v > hi) { bad = true; }
  return { label, v: round2(v), range, bad };
}

function showGrade(res) {
  const box = $('result');
  box.textContent = '';
  const m = res.metrics;
  if (!m) {
    box.textContent = '走らせたが1回も記録が取れなかった。';
    return;
  }

  const h = document.createElement('h2');
  h.textContent = `合格条件  (面${m.stage} / ${m.runs}回 / ボット=貪欲)`;
  box.appendChild(h);

  const tb = document.createElement('table');
  tb.innerHTML = '<tr><th></th><th>項目</th><th>値</th><th>合格の範囲</th></tr>';
  /* **しきい値は metrics.py が持っている。** ここでは写さない。 */
  for (const g of (m.gates || [])) {
    const row = gateRow(m, g);
    if (!row) { continue; }
    const tr = document.createElement('tr');
    tr.className = row.none ? '' : (row.bad ? 'ng' : 'ok');
    tr.innerHTML = `<td class="verd">${row.none ? '—' : (row.bad ? '×' : '○')}</td>`;
    for (const t of [row.label, row.v, row.range]) {
      const td = document.createElement('td');
      td.textContent = t;
      tr.appendChild(td);
    }
    tb.appendChild(tr);
  }
  box.appendChild(tb);

  const t2 = document.createElement('table');
  t2.innerHTML = '<tr><th>クリア率</th><th>秒</th><th>放出</th><th>体温</th><th>残り炭</th></tr>';
  const tr = document.createElement('tr');
  tr.className = 'ok';
  for (const t of [m.clear_rate, m.clear_secs ?? '-', m.burst, m.temp_max, m.coal_left]) {
    const td = document.createElement('td');
    td.textContent = t;
    tr.appendChild(td);
  }
  t2.appendChild(tr);
  box.appendChild(t2);

  const e = res.emotion;
  if (e) {
    const h2 = document.createElement('h2');
    h2.textContent = `感情  (ボット=賢い / ${e.runs}回)`;
    box.appendChild(h2);
    const t3 = document.createElement('table');
    t3.innerHTML = '<tr><th>軸</th><th>値</th><th></th></tr>';
    for (const [lab, v] of [['ストレス', e.stress], ['爽快感', e.exhil]]) {
      const r3 = document.createElement('tr');
      r3.className = 'ok';
      /* **文字として差し込む。** いまは数しか来ないが、向こうの出力が変わった日に
         組み立て方のせいで困らないようにしておく。 */
      for (const [t, cls] of [[lab, ''], [round2(v), ''], [bar(v), 'bar']]) {
        const td = document.createElement('td');
        td.className = cls;
        td.textContent = t;
        r3.appendChild(td);
      }
      t3.appendChild(r3);
    }
    box.appendChild(t3);
    const p = document.createElement('p');
    const st = document.createElement('strong');
    st.textContent = e.verdict;
    p.append('判定: ', st);
    box.appendChild(p);
    if (e.why) {
      const w = document.createElement('p');
      w.className = 'note';
      w.textContent = e.why;
      box.appendChild(w);
    }
    const n = document.createElement('p');
    n.className = 'note';
    n.textContent = '感情の軸はまだ較正していない。面どうしの比較にだけ使うこと。';
    box.appendChild(n);
  }
}

async function grade() {
  if (!(await validate())) {
    $('result').textContent = 'まず読み込める面にしてから。';
    return;
  }
  const btn = $('grade');
  btn.disabled = true;
  $('result').innerHTML = '<p class="busy">採点中… (回数を増やすほど待ちます)</p>';
  try {
    const runs = Math.max(1, Math.min(64, +$('runs').value || 8));
    const res = await api(`/api/grade?runs=${runs}`, post(toFile()));
    if (res.ok) { showGrade(res); }
    else { $('result').textContent = res.error || '採点に失敗した'; }
  } catch (err) {
    $('result').textContent = String(err);
  } finally {
    btn.disabled = false;
  }
}

/* ------------------------------------------------------------------ *
 * ヘッダの操作
 * ------------------------------------------------------------------ */
async function refreshFiles() {
  const r = await api('/api/stages');
  const sel = $('files');
  sel.textContent = '';
  const o = document.createElement('option');
  o.value = ''; o.textContent = '(保存したもの)';
  sel.appendChild(o);
  for (const n of (r.names || [])) {
    const op = document.createElement('option');
    op.value = n; op.textContent = n;
    sel.appendChild(op);
  }
}

function wire() {
  const sel = $('stage');
  for (let i = 1; i <= 15; i++) {
    const o = document.createElement('option');
    o.value = String(i);
    o.textContent = `${i}  (第${Math.min(5, Math.floor((i - 1) / 3) + 1)}坑)`;
    sel.appendChild(o);
  }
  sel.onchange = () => { snapshot(); ST.stage = +sel.value; changed(); };
  $('loop').onchange = () => { snapshot(); ST.loop = $('loop').checked ? 1 : 0; changed(); };
  $('gridon').onchange = draw;
  $('undo').onclick = undo;
  $('undo').disabled = true;
  $('grade').onclick = grade;

  $('gen').onclick = async () => {
    const q = `stage=${ST.stage}&loop=${ST.loop}&seed=${encodeURIComponent($('seed').value.trim())}`;
    const r = await api('/api/generate?' + q);
    if (!r.ok) { say(r.error); return; }
    snapshot();
    fromFile(r.stage);
    say('生成した面を読み込んだ。ここから直す。');
    changed();
  };

  $('open').onclick = async () => {
    const n = $('files').value;
    if (!n) { say('開くものを選んでください'); return; }
    const r = await api('/api/open?name=' + encodeURIComponent(n));
    if (!r.ok) { say(r.error); return; }
    snapshot();
    fromFile(r.stage);
    $('name').value = n;
    say(n + ' を開いた');
    changed();
  };

  $('save').onclick = async () => {
    let n = $('name').value.trim();
    if (!/\.json$/.test(n)) { n += '.json'; }
    /* **同じ名前があれば一声かける。** 試作が黙って消えるのを防ぐ。 */
    const have = [...$('files').options].some((o) => o.value === n);
    if (have && !confirm(n + ' は既にあります。上書きしますか？')) { return; }
    const d = toFile();
    const r = await api('/api/save?name=' + encodeURIComponent(n), post(d));
    if (!r.ok) { say(r.error); return; }
    const lost = (ST.rockOrder && !d.rockOrder) ? '  (岩の並び順は落とした: 升が変わったため)' : '';
    say(r.path + ' に保存した' + lost);
    refreshFiles();
  };
}

async function boot() {
  const p = await api('/api/palette');
  if (p.ok) { DIRT = p.dirt; }
  wire();
  blank();
  syncHeader();
  loadArt(() => { buildBrushes(); changed(); });
  refreshFiles();
}

boot();
