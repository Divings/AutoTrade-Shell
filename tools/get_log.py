import os
import re
from datetime import datetime, timedelta

def get_fx_debug_log(
    last_temp_path="/opt/Innovations/System/last_temp/last_temp.txt",
    target_filename="fx_debug_log.txt"
):
    if not os.path.exists(last_temp_path):
        print(f"[エラー] 記録ファイルが存在しません: {last_temp_path}")
        return

    try:
        with open(last_temp_path, "r") as f:
            content = f.read()

        match = re.search(r'(/tmp/[^\s]+)', content)
        if not match:
            print("[エラー] 一時ディレクトリのパスが見つかりませんでした。")
            return

        temp_dir = match.group(1)
        log_path = os.path.join(temp_dir, target_filename)

        if not os.path.isfile(log_path):
            print(f"[エラー] ログファイルが見つかりません: {log_path}")
            return

        print(f"[情報] ログファイル: {log_path}")

        now = datetime.now()
        cutoff = now - timedelta(hours=1)

        with open(log_path, "r") as log_file:
            for line in log_file:
                # 例: 2026-01-14 12:34:56
                m = re.match(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})', line)
                if not m:
                    continue

                log_time = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S")
                if log_time >= cutoff:
                    print(line, end="")


    except Exception as e:
        print(f"[例外エラー] {e}")

# 実行
get_fx_debug_log()

