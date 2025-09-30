#!/usr/bin/env bash

# Fail fast on errors
set -euo pipefail

# Namida Build Helper
# ------------------
# This script helps package Namida into a Windows-compatible installer.
# It does the following:
#  1. Extracts an existing Inno installer if one exists
#  2. Packages the app directory into a zip
#  3. Builds our custom XP-compatible launcher
#  4. Combines everything into a single EXE
#  5. Saves the result in the dist folder

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
APP_DIR="$ROOT_DIR/app"
BIN_DIR="$ROOT_DIR/bin"
ASM_DIR="$ROOT_DIR/asm"
DIST_DIR="$ROOT_DIR/dist"

ZIP_PATH="$BIN_DIR/zip.zip"
LAUNCHER_TARGET="$BIN_DIR/namidiaforxp.exe"
FINAL_EXE="$DIST_DIR/namidiaforxp_installer.exe"

mkdir -p "$BIN_DIR"
mkdir -p "$DIST_DIR"

echo "Looking for existing installer to extract..."
if [ -f "$ASM_DIR/Namida-x86_64-Installer.exe" ]; then
    # Try to extract if innoextract is available
    if command -v innoextract >/dev/null 2>&1; then
        echo "Found installer! Extracting to app..."
        innoextract -d "$ROOT_DIR" "$ASM_DIR/Namida-x86_64-Installer.exe" || true
    else
        echo "Couldn't find innoextract tool - skipping extraction step."
    fi
else
    echo "No existing installer found - continuing with fresh build."
fi

echo "Packaging app files..."
# Package everything into a sorted zip for consistency
if [ -d "$APP_DIR" ]; then
    echo "Creating zip at: $ZIP_PATH"
    (cd "$APP_DIR" && zip -r -q "$ZIP_PATH" .)
    echo "✓ Zip created successfully"
else
    echo "❌ Error: Couldn't find the app directory at '$APP_DIR'" >&2
    exit 2
fi

echo "Building the launcher..."
# Try to find and use the Makefile
if [ -f "$BIN_DIR/Makefile" ] || [ -f "$ROOT_DIR/bin/Makefile" ] || [ -f "$ROOT_DIR/bin/makefile" ]; then
    echo "Found Makefile - running build..."
    (cd "$ROOT_DIR/bin" && make clean || true && make)
    echo "✓ Build completed"
else
    # One last try in the repo root
    if [ -f "$ROOT_DIR/bin/Makefile" ]; then
        echo "Found Makefile in repo root - building..."
        (cd "$ROOT_DIR/bin" && make clean || true && make)
    else
        echo "⚠️  No Makefile found - skipping launcher build"
    fi
fi

if [ ! -f "$LAUNCHER_TARGET" ]; then
	echo "WARNING: launcher $LAUNCHER_TARGET not produced; attempting to find any built exe in $BIN_DIR"
	LAUNCHER_TARGET_CANDIDATE=$(ls -1 "$BIN_DIR"/*.exe 2>/dev/null | head -n1 || true)
	if [ -n "$LAUNCHER_TARGET_CANDIDATE" ]; then
		LAUNCHER_TARGET="$LAUNCHER_TARGET_CANDIDATE"
		echo "  using $LAUNCHER_TARGET"
	else
		echo "ERROR: no launcher executable found. Aborting." >&2
		exit 3
	fi
fi

echo "Creating final installer..."
# Combine the launcher with our zip payload
# We add a small footer (8 bytes) to help find the zip later:
#   - 4 bytes: Magic number 0x3050495A (little-endian)
#   - 4 bytes: Length of the zip payload
python3 - <<'PY'
import struct, sys
from pathlib import Path
root = Path("./")
bin_dir = Path("bin/")
zip_path = Path("bin/zip.zip")
launcher = Path("bin/namidiaforxp.exe")
out = Path("dist/namidiaforxp_installer.exe")
if not launcher.exists():
		print('launcher not found', file=sys.stderr); sys.exit(1)
if not zip_path.exists():
		print('zip payload not found', file=sys.stderr); sys.exit(2)
data = zip_path.read_bytes()
with launcher.open('rb') as f:
		exe = f.read()
with out.open('wb') as f:
		f.write(exe)
		f.write(data)
		# footer: magic 0x3050495A then 32-bit little-endian length of payload
		f.write(struct.pack('<I', 0x3050495A))
		f.write(struct.pack('<I', len(data)))
print('wrote', out)
PY

echo -e "\nBuild finished!"
echo "Generated files:"
ls -l "$DIST_DIR"

echo -e "\nYIPEEE: $FINAL_EXE"

exit 0
