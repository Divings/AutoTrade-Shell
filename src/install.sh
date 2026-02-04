#!/usr/bin/env bash
set -euo pipefail

# ===== configurable =====
BIN_SRC="${1:-./tradeshell}"             # ビルド済みバイナリ
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
BIN_NAME="${BIN_NAME:-tradeshell}"
BIN_DST="${INSTALL_DIR}/${BIN_NAME}"

# optional: dedicated config dir (not mandatory)
ETC_DIR="${ETC_DIR:-/etc/tradeshell}"
# ========================

die() { echo "ERROR: $*" >&2; exit 1; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

need_cmd install
need_cmd id

if [[ "$(id -u)" -ne 0 ]]; then
  die "run as root (e.g. sudo $0)"
fi

[[ -f "$BIN_SRC" ]] || die "binary not found: $BIN_SRC"
[[ -x "$BIN_SRC" ]] || die "binary is not executable: $BIN_SRC"

echo "[*] Installing $BIN_SRC -> $BIN_DST"

# create dirs
install -d -m 0755 "$INSTALL_DIR"
install -d -m 0755 "$ETC_DIR"

# install binary (0755 root:root)
install -m 0755 -o root -g root "$BIN_SRC" "$BIN_DST"

echo "[✓] Installed: $BIN_DST"

# quick sanity check
echo "[*] Sanity check: run 'help' then 'exit'"
"$BIN_DST" <<'EOF' >/dev/null
help
exit
EOF
echo "[✓] Sanity check OK"

cat <<EOF

Done.

Run:
  ${BIN_NAME}

Tip:
  If you want command history, build with readline:
    sudo dnf install -y readline-devel
    gcc ... -DUSE_READLINE ... -lreadline

EOF
