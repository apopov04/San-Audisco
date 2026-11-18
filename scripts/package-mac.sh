#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
APP="${2:-$ROOT/dist/AudioBridge.app}"
mkdir -p "$APP/Contents/MacOS"
cp "$BUILD/audiobridge" "$APP/Contents/MacOS/bridge-engine"
cp "$BUILD/bridge-agent" "$APP/Contents/MacOS/bridge-agent"
swiftc -target "$(uname -m)-apple-macos13.0" -O -parse-as-library "$ROOT/apps/macos/AudioBridge.swift" -o "$APP/Contents/MacOS/AudioBridge" -framework SwiftUI -framework AppKit -framework AVFoundation -framework ServiceManagement
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>CFBundleExecutable</key><string>AudioBridge</string><key>CFBundleIdentifier</key><string>local.audiobridge.desktop</string><key>CFBundleName</key><string>San Audisco</string><key>CFBundleDisplayName</key><string>San Audisco</string><key>CFBundleVersion</key><string>9</string><key>CFBundleShortVersionString</key><string>0.2.7</string><key>NSMicrophoneUsageDescription</key><string>Relay your selected microphone to your paired computer and voice apps.</string><key>NSLocalNetworkUsageDescription</key><string>Discover and connect to your San Audisco room on your local network.</string><key>LSMinimumSystemVersion</key><string>13.0</string></dict></plist>
PLIST
mkdir -p "$APP/Contents/Resources/licenses"
cp "$ROOT/third_party/licenses/"*.txt "$APP/Contents/Resources/licenses/"
cp "$ROOT/third_party/json/LICENSE.MIT" "$APP/Contents/Resources/licenses/nlohmann-json.txt"
cp "$ROOT/README.md" "$ROOT/VALIDATION.md" "$ROOT/HARDWARE_TEST.md" "$ROOT/PRIVATE_SETUP.md" "$APP/Contents/Resources/"
mkdir -p "$APP/Contents/Resources/docs"
cp "$ROOT/docs/"*.md "$APP/Contents/Resources/docs/"
codesign --force --deep --sign - "$APP"
echo "$APP"
