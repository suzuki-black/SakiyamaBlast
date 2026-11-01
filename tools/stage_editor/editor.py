#!/usr/bin/env python3
"""SakiyamaBlast - 面エディタ (ローカルで走る道具。ROM には入らない)

    make editor                                      # 立ち上げてブラウザを開く
    python3 tools/stage_editor/editor.py --port 8765 --no-open

盤面を手で置いて、**その場で採点を回す。** 採点は tools/hostsim の sim /
metrics.py / emotion.py をそのまま呼ぶので、判定の基準はコマンドライン版と
同じものになる (エディタ側に採点の規則を持たない)。

面データの形式は docs/STAGE_FORMAT.md。**読み書きと整合の検査は sim に任せて
ある**ので、ここは JSON を組み立てて投げ、返ってきた文句をそのまま見せるだけ。

**127.0.0.1 にだけ開き、起動ごとの合図 (token) を要求する。** 外から届かない
だけでは足りない ── 利用者のブラウザで開いている別のページが localhost へ
投げてくる形があるので、URL に入れた合図を見て弾く。この道具はファイルを
書き、子プロセスを起こすので、そこは締めておく。
"""
import argparse
import http.server
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # リポジトリの根
SIM = os.path.join(ROOT, "tools", "hostsim", "sim")
METRICS = os.path.join(ROOT, "tools", "hostsim", "metrics.py")
EMOTION = os.path.join(ROOT, "tools", "hostsim", "emotion.py")
STAGES = os.path.join(HERE, "stages")                  # 保存先。ここから出さない

MAX_BODY = 256 * 1024          # 面データは数KB。これより大きいものは受けない
NAME_OK = re.compile(r"\A[0-9A-Za-z_-]{1,64}\.json\Z")

# ボットの腕。**metrics.py / emotion.py の --bot と揃えること。**
# 採点の数字は腕に強く引きずられるので (面5 はストレスが 54 と 2 に分かれた)、
# 両方の道具に同じ腕を渡し、どちらで測ったかを画面に出す。
BOTS = ("greedy", "smart")

# 幕ごとの土の2色 (MSX の R/G/B 0-7)。
# **src/levels.c の set_dirt_palette() と必ず一致させること。**
# 絵のほうは触らなくてよい (tools/palette.py の RUNTIME が示すとおり、
# tiles.png / sprites.png は土の色番号を使っていない)。
DIRT_BY_ACT = {
    1: ((4, 2, 1), (3, 1, 1)),     # 第一坑 炭層まじりの茶
    2: ((3, 3, 2), (2, 2, 1)),     # 第二坑 硬い岩盤の灰
    3: ((2, 2, 4), (1, 1, 3)),     # 第三坑 湧水を含む青
    4: ((2, 1, 3), (1, 0, 2)),     # 第四坑 深い藍
    5: ((1, 1, 2), (2, 2, 3)),     # 第五坑 ほとんど闇 (ここだけ粒が明るい)
}

TOKEN = secrets.token_urlsafe(16)


def run(cmd, timeout=180):
    """子プロセスを起こす。**引数は必ず配列。** シェルを通さない。"""
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    return p.returncode, p.stdout, p.stderr


def with_tmp_stage(data, fn):
    """面データを一時ファイルへ書いて fn(path) を呼ぶ。後始末までやる。"""
    d = tempfile.mkdtemp(prefix="sakiyama-stage-")
    try:
        path = os.path.join(d, "stage.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        return fn(path)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def api_validate(data):
    """sim に読ませて、受け取ってもらえるかだけを見る。"""
    def go(path):
        rc, out, err = run([SIM, "--load", path, "--state"], timeout=30)
        if rc == 0:
            return {"ok": True}
        # sim は「どこが悪いか」を stderr に1行で書く。行頭の一時パスは外す。
        msg = (err or out).strip().splitlines()
        msg = msg[-1] if msg else "読み込みに失敗した"
        return {"ok": False, "error": re.sub(r"\A\S*stage\.json:\d+:\s*", "", msg)}
    return with_tmp_stage(data, go)


def api_grade(data, runs, frames, bot):
    """metrics.py と emotion.py をそのまま呼ぶ。**採点の規則はあちらが持つ。**

    **腕は両方に同じものを渡す。** 片方を貪欲、片方を先読みで回すと、
    並べた2枚が別の遊び手の話になる (面5 はストレスが 54 と 2 に分かれた)。
    """
    def go(path):
        out = {"bot": bot}
        rc, so, se = run([sys.executable, METRICS, "--load", path, "--bot", bot,
                          "--runs", str(runs), "--frames", str(frames), "--json"])
        if rc != 0:
            return {"ok": False, "error": (se or so).strip()[-400:]}
        got = json.loads(so or "[]")
        out["metrics"] = got[0] if got else None

        rc, so, se = run([sys.executable, EMOTION, "--load", path, "--bot", bot,
                          "--runs", str(max(2, runs // 2)), "--frames", str(frames), "--json"]
                         + (["--ura"] if data.get("loop") else []))
        if rc == 0:
            got = json.loads(so or "[]")
            out["emotion"] = got[0] if got else None
        else:
            out["emotion"] = None
        out["ok"] = True
        return out
    return with_tmp_stage(data, go)


def api_generate(stage, loop, lseed):
    """生成した面を面データとして受け取る (手で直す下敷きにする)。"""
    cmd = [SIM, "--stage", str(int(stage)), "--dump"]
    if loop:
        cmd.append("--ura")
    if lseed:
        cmd += ["--layout-seed", str(int(lseed))]
    rc, out, err = run(cmd, timeout=60)
    if rc != 0:
        return {"ok": False, "error": (err or out).strip()[-400:]}
    return {"ok": True, "stage": json.loads(out)}


def safe_stage_path(name):
    """保存先を stages/ の中へ閉じ込める。"""
    if not NAME_OK.match(name or ""):
        return None
    return os.path.join(STAGES, name)


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "SakiyamaStageEditor"

    def log_message(self, fmt, *args):            # 1行ずつの既定の log は邪魔
        pass

    # ---- 返事 ----
    def send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path, ctype):
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    # ---- 名乗り ----
    def ok_host(self):
        """**自分の名前で来たものだけ相手にする。**
        利用者のブラウザで開いた別のページが、名前解決を自分のアドレスへ
        向け直して (DNS リバインディング) 同一オリジンのふりをする形があるので、
        Host が localhost でなければ断る。ここは合図の要らない静的ファイルにも効く。"""
        host = (self.headers.get("Host") or "").strip()
        if not host:
            return True                      # HTTP/1.0 など。Host 無しは素通し
        port = self.server.server_address[1]
        ok = {"127.0.0.1:%d" % port, "localhost:%d" % port, "[::1]:%d" % port,
              "127.0.0.1", "localhost", "[::1]"}
        if host in ok:
            return True
        self.send_json({"ok": False, "error": "この名前では開けません (127.0.0.1 で開いてください)"}, 403)
        return False

    # ---- 合図 ----
    def ok_token(self, q):
        """起動ごとの合図を確かめる。**別のページから叩かれるのを弾く。**
        合図は URL の**断片** (#t=...) に置いてある。問い合わせ (?t=...) だと
        Referer やブラウザの履歴に残るため。断片はサーバへ送られないので、
        画面の JS が読み取って毎回ここへ添える。"""
        got = q.get("t", [""])[0]
        try:
            if secrets.compare_digest(got, TOKEN):
                return True
        except TypeError:
            pass                             # 非 ASCII。合うはずがない
        self.send_json({"ok": False, "error": "合図が違う (エディタを開き直してください)"}, 403)
        return False

    def qint(self, q, name, dflt, lo, hi):
        """問い合わせの数をひとつ。数でなければ既定へ倒す (落ちない)。"""
        try:
            v = int(q.get(name, [str(dflt)])[0])
        except (TypeError, ValueError):
            v = dflt
        return max(lo, min(hi, v))

    def read_body(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = -1
        if n <= 0 or n > MAX_BODY:
            self.send_json({"ok": False, "error": "本文の大きさが変です"}, 400)
            return None
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as e:
            self.send_json({"ok": False, "error": "JSON として読めない: %s" % e}, 400)
            return None

    # ---- GET ----
    def do_GET(self):
        if not self.ok_host():
            return
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        p = u.path

        if p in ("/", "/index.html"):
            return self.send_file(os.path.join(HERE, "index.html"), "text/html; charset=utf-8")
        if p == "/app.js":
            return self.send_file(os.path.join(HERE, "app.js"), "text/javascript; charset=utf-8")
        if p == "/app.css":
            return self.send_file(os.path.join(HERE, "app.css"), "text/css; charset=utf-8")
        if p == "/art/tiles.png":
            return self.send_file(os.path.join(ROOT, "assets", "tiles.png"), "image/png")
        if p == "/art/sprites.png":
            return self.send_file(os.path.join(ROOT, "assets", "sprites.png"), "image/png")

        if not p.startswith("/api/"):
            return self.send_error(404)
        if not self.ok_token(q):
            return

        if p == "/api/palette":
            return self.send_json({
                "ok": True,
                "dirt": {str(k): v for k, v in DIRT_BY_ACT.items()},
            })
        if p == "/api/stages":
            try:
                names = sorted(f for f in os.listdir(STAGES) if NAME_OK.match(f))
            except OSError:
                names = []
            return self.send_json({"ok": True, "names": names})
        if p == "/api/open":
            path = safe_stage_path(q.get("name", [""])[0])
            if not path:
                return self.send_json({"ok": False, "error": "名前が使えません"}, 400)
            try:
                with open(path, encoding="utf-8") as f:
                    return self.send_json({"ok": True, "stage": json.load(f)})
            except OSError:
                return self.send_json({"ok": False, "error": "その面データを開けません"}, 400)
            except ValueError:
                return self.send_json({"ok": False, "error": "その面データは JSON として読めません"}, 400)
        if p == "/api/generate":
            seed = q.get("seed", [""])[0].strip()
            if seed and not seed.isdigit():
                return self.send_json({"ok": False, "error": "種は数で書いてください"}, 400)
            return self.send_json(api_generate(
                self.qint(q, "stage", 1, 1, 15),
                q.get("loop", ["0"])[0] == "1",
                seed or None))
        return self.send_error(404)

    # ---- POST ----
    def do_POST(self):
        if not self.ok_host():
            return
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        if not u.path.startswith("/api/"):
            return self.send_error(404)
        if not self.ok_token(q):
            return
        body = self.read_body()
        if body is None:
            return

        if u.path == "/api/validate":
            return self.send_json(api_validate(body))
        if u.path == "/api/grade":
            runs = self.qint(q, "runs", 8, 1, 64)
            frames = self.qint(q, "frames", 5400, 600, 20000)
            # **腕は名前を検査してから通す。** そのまま返事に載せる値なので、
            # 知らない文字列を持ち帰らせない。
            bot = q.get("bot", ["greedy"])[0]
            if bot not in BOTS:
                return self.send_json({"ok": False, "error": "知らないボット"}, 400)
            try:
                return self.send_json(api_grade(body, runs, frames, bot))
            except subprocess.TimeoutExpired:
                return self.send_json({"ok": False, "error": "採点が時間切れ (回数か長さを下げてください)"})
        if u.path == "/api/save":
            path = safe_stage_path(q.get("name", [""])[0])
            if not path:
                return self.send_json({"ok": False, "error": "名前は英数字と _ - だけ、末尾は .json"}, 400)
            os.makedirs(STAGES, exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(body, f, ensure_ascii=False, indent=2)
                f.write("\n")
            return self.send_json({"ok": True, "path": os.path.relpath(path, ROOT)})
        return self.send_error(404)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true", help="ブラウザを開かない")
    a = ap.parse_args()

    if not os.path.exists(SIM):
        sys.exit("%s がありません。make hostsim を先に実行してください。" % os.path.relpath(SIM, ROOT))
    os.makedirs(STAGES, exist_ok=True)

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    url = "http://127.0.0.1:%d/#t=%s" % (srv.server_address[1], TOKEN)
    print("面エディタ: %s" % url)
    print("(127.0.0.1 にだけ開いています。止めるには Ctrl-C)")
    if not a.no_open:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n終了しました。")


if __name__ == "__main__":
    main()
