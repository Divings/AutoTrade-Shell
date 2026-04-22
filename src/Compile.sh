#!/usr/bin/env bash
set -euo pipefail

SRC="${1:-tradeshell.c}"
OUT="${2:-tradeshell}"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:-} -O2 -Wall -Wextra"
LDFLAGS="${LDFLAGS:-}"

if [[ ! -f "$SRC" ]]; then
  echo "ERROR: source file not found: $SRC" >&2
  exit 1
fi

echo "[*] Building: $SRC -> $OUT"

has_readline=0
readline_cflags=""
readline_libs=""

# まず pkg-config を優先
if command -v pkg-config >/dev/null 2>&1; then
  if pkg-config --exists readline; then
    has_readline=1
    readline_cflags="$(pkg-config --cflags readline)"
    readline_libs="$(pkg-config --libs readline)"
  fi
fi

# pkg-config で取れなかった場合のフォールバック
if [[ $has_readline -eq 0 ]]; then
  if [[ -f "/usr/include/readline/readline.h" ]]; then
    if [[ -f "/usr/lib64/libreadline.so" ]] || [[ -f "/usr/lib/libreadline.so" ]]; then
      has_readline=1
      readline_libs="-lreadline"
    elif command -v ldconfig >/dev/null 2>&1; then
      if ldconfig -p 2>/dev/null | grep -q "libreadline\.so"; then
        has_readline=1
        readline_libs="-lreadline"
      fi
    fi
  fi
fi

if [[ $has_readline -eq 1 ]]; then
  echo "[+] readline detected: enabling history support"
  CFLAGS="$CFLAGS -DUSE_READLINE $readline_cflags"
  LDFLAGS="$LDFLAGS $readline_libs"
else
  echo "[-] readline not detected: building without history support"
  echo "    install with: sudo dnf install -y readline-devel pkgconf-pkg-config"
fi

set -x
"$CC" $CFLAGS -o "$OUT" "$SRC" $LDFLAGS
set +x

echo "[✓] Done: ./$OUT"
echo "    Run: ./$OUT"