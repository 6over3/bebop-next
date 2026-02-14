#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SWIFT_DIR="$REPO_ROOT/plugins/swift"
SCHEMAS="$REPO_ROOT/bebop/schemas"
BEBOPC="$REPO_ROOT/build/bin/bebopc"

if [ ! -x "$BEBOPC" ]; then
  echo "error: bebopc not found at $BEBOPC"
  echo "       run 'make' from repo root first"
  exit 1
fi

echo "Building bebopc-gen-swift..."
cd "$REPO_ROOT"
swift build --product bebopc-gen-swift -q
PLUGIN="$REPO_ROOT/.build/debug/bebopc-gen-swift"

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT


echo "Generating descriptor.bb.swift + plugin.bb.swift..."
"$BEBOPC" build \
  "$SCHEMAS/bebop/descriptor.bop" \
  "$SCHEMAS/bebop/plugin.bop" \
  -I "$SCHEMAS" \
  --plugin=swift="$PLUGIN" \
  --swift_out="$TMPDIR/plugin" \
  -q 2>/dev/null
cp "$TMPDIR/plugin/descriptor.bb.swift" "$SWIFT_DIR/Sources/BebopPlugin/Generated/"
cp "$TMPDIR/plugin/plugin.bb.swift" "$SWIFT_DIR/Sources/BebopPlugin/Generated/"


echo "Generating rpc.bb.swift..."
"$BEBOPC" build \
  "$SCHEMAS/bebop/rpc.bop" \
  -I "$SCHEMAS" \
  --plugin=swift="$PLUGIN" \
  --swift_out="$TMPDIR/rpc" \
  -q 2>/dev/null
sed '/^import SwiftBebop$/d' "$TMPDIR/rpc/rpc.bb.swift" > "$SWIFT_DIR/Sources/SwiftBebop/RPC/rpc.bb.swift"

echo "Generating test_service.bb.swift..."
"$BEBOPC" build \
  "$SWIFT_DIR/Tests/SwiftBebopTests/test_service.bop" \
  -I "$SCHEMAS" \
  --plugin=swift="$PLUGIN" \
  -D Services=both \
  --swift_out="$TMPDIR/test" \
  -q 2>/dev/null
cp "$TMPDIR/test/test_service.bb.swift" "$SWIFT_DIR/Tests/SwiftBebopTests/"

echo "Done."
