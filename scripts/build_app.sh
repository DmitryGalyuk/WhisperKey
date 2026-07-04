#!/usr/bin/env bash

# # собрать release и получить .app
# scripts/build_app.sh

# # или для debug
# BUILD_CFG=debug SWIFT_CFG="-c debug" scripts/build_app.sh

set -euo pipefail

APP_NAME="WhisperKey"
BUILD_CFG="release"   # release or debug
SWIFT_CFG="-c ${BUILD_CFG}"

# paths
SWIFT_BUILD_DIR=".build/${BUILD_CFG}"
BIN_PATH="${SWIFT_BUILD_DIR}/${APP_NAME}"
DIST_DIR="dist"
APP_DIR="${DIST_DIR}/${APP_NAME}.app"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RES_DIR="${CONTENTS_DIR}/Resources"

echo "Cleaning old build..."
rm -rf "${DIST_DIR}" "${APP_DIR}"

echo "Building Swift package (${BUILD_CFG})..."
swift build ${SWIFT_CFG}

if [ ! -f "${BIN_PATH}" ]; then
  echo "ERROR: built binary not found at ${BIN_PATH}"
  exit 1
fi

echo "Creating .app bundle..."
mkdir -p "${MACOS_DIR}" "${RES_DIR}"
cp "${BIN_PATH}" "${MACOS_DIR}/${APP_NAME}"
chmod +x "${MACOS_DIR}/${APP_NAME}"

cat > "${CONTENTS_DIR}/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>WhisperKey</string>
  <key>CFBundleDisplayName</key><string>WhisperKey</string>
  <key>CFBundleIdentifier</key><string>com.example.WhisperKey</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleExecutable</key><string>WhisperKey</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/> <!-- hide Dock icon; remove if you want a Dock icon -->
</dict>
</plist>
PLIST

# Optional: codesign (set CODESIGN_ID env var to sign)
if [ -n "${CODESIGN_ID:-}" ]; then
  echo "Codesigning with ${CODESIGN_ID}..."
  codesign --timestamp --sign "${CODESIGN_ID}" "${MACOS_DIR}/${APP_NAME}"
fi

echo "Built ${APP_DIR}"
echo "You can run: open \"${APP_DIR}\""