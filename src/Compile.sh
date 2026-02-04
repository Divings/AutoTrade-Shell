#!/usr/bin/env bash
set -euo pipefail

SRC="${1:-tradeshell.c}"
OUT="${2:-tradeshell}"

CC="${CC:-gcc}"
CFLAGS="-O2 -Wall -Wextra"
LDFLAGS=""

if [[ ! -f "$SRC" ]]; then
  echo "ERROR: source file not found: $SRC" >&2
  exit 1
fi

echo "[*] Building: $SRC -> $OUT"

# Detect readline (Oracle Linux: readline-devel provides libreadline.so and headers)
has_readline=0

# Quick header check
if [[ -f "/usr/include/readline/readline.h" ]]; then
  has_readline=1
fi

# Library presence check (common locations)
if [[ $has_readline -eq 1 ]]; then
  if [[ -f "/usr/lib64/libreadline.so" ]] || [[ -f "/usr/lib/libreadline.so" ]]; then
    has_readline=1
  else
    # fallback: try ldconfig cache if available
    if command -v ldconfig >/dev/null 2>&1; then
      if ! ldconfig -p 2>/dev/null | grep -q "libreadline\.so"; then
        has_readline=0
      fi
    else
      has_readline=0
    fi
  fi
fi

if [[ $has_readline -eq 1 ]]; then
  echo "[+] readline detected: enabling history support"
  CFLAGS="$CFLAGS -DUSE_READLINE"
  LDFLAGS="$LDFLAGS -lreadline"
else
  echo "[-] readline not detected: building without it"
  echo "    (optional) install: sudo dnf install -y readline-devel"
fi

set -x
"$CC" $CFLAGS -o "$OUT" "$SRC" $LDFLAGS
set +x

echo "[✓] Done: ./$OUT"
echo "    Run: ./$OUT"
