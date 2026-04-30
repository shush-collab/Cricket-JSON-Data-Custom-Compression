#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

for input in cric/*.json uploads/*.json 1461915.json; do
  [ -f "$input" ] || continue
  name="$(basename "$input")"
  cjdc="$tmpdir/$name.cjdc"
  out="$tmpdir/$name.out.json"
  compress_log="$tmpdir/$name.compress.log"
  ./compress "$input" "$cjdc" >"$compress_log"
  grep -q "Exact delivery fallbacks: 0" "$compress_log"
  ./decompress "$cjdc" "$out" >/dev/null
  cmp -s "$input" "$out"
done

cat >"$tmpdir/noncanonical.json" <<'JSON'
{"b":2,
 "a": [1, 2, 3]}
JSON
./compress "$tmpdir/noncanonical.json" "$tmpdir/noncanonical.cjdc" >"$tmpdir/noncanonical.compress.log"
grep -q "raw JSON fallback: yes" "$tmpdir/noncanonical.compress.log"
./decompress "$tmpdir/noncanonical.cjdc" "$tmpdir/noncanonical.out.json" >/dev/null
cmp -s "$tmpdir/noncanonical.json" "$tmpdir/noncanonical.out.json"

python3 - "$tmpdir/noncanonical.cjdc" "$tmpdir/corrupt.cjdc" <<'PY'
import sys
src, dst = sys.argv[1:3]
data = bytearray(open(src, "rb").read())
data[-1] ^= 0x01
open(dst, "wb").write(data)
PY
if ./decompress "$tmpdir/corrupt.cjdc" "$tmpdir/corrupt.out.json" >/dev/null 2>&1; then
  echo "corrupt input decoded successfully"
  exit 1
fi

echo "roundtrip ok"
