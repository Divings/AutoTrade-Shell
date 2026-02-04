#!/usr/bin/env python3
import os
import shutil

TMP_DIR = "/tmp"
TARGET_LOG = "fx_debug_log.txt"
LAST_TEMP_FILE = "/opt/Innovations/System/last_temp/last_temp.txt"

def load_active_tmp_dir():
    """
    last_temp.txt から現在使用中の /tmp ディレクトリを取得
    """
    if not os.path.exists(LAST_TEMP_FILE):
        return None

    with open(LAST_TEMP_FILE, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f.readlines() if line.strip()]

    # 最後の行が /tmp/xxxx である前提
    for line in reversed(lines):
        if line.startswith("/tmp/"):
            return os.path.realpath(line)

    return None


def cleanup_tmp_dirs():
    active_dir = load_active_tmp_dir()

    if active_dir:
        print(f"[INFO] 使用中ディレクトリ: {active_dir}")
    else:
        print("[WARN] 使用中ディレクトリが取得できません")

    for root, dirs, files in os.walk(TMP_DIR):
        if TARGET_LOG in files:
            root_real = os.path.realpath(root)

            if active_dir and root_real == active_dir:
                print(f"[SKIP] 使用中ディレクトリのため除外: {root_real}")
                continue

            print(f"[DELETE] 未使用ディレクトリ削除: {root_real}")
            try:
                shutil.rmtree(root_real)
            except Exception as e:
                print(f"[ERROR] 削除失敗: {root_real} ({e})")


if __name__ == "__main__":
    cleanup_tmp_dirs()

