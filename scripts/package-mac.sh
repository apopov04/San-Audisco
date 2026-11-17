#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
APP="$ROOT/dist/AudioBridge.app"
mkdir -p "$APP/Contents/MacOS"
cp "$BUILD/audiobridge" "$APP/Contents/MacOS/bridge-engine"
swiftc -O -parse-as-library "$ROOT/apps/macos/AudioBridge.swift" -o "$APP/Contents/MacOS/AudioBridge" -framework SwiftUI -framework AppKit
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>CFBundleExecutable</key><string>AudioBridge</string><key>CFBundleIdentifier</key><string>local.audiobridge.desktop</string><key>CFBundleName</key><string>AudioBridge</string><key>CFBundleVersion</key><string>1</string><key>CFBundleShortVersionString</key><string>0.1.0</string><key>NSMicrophoneUsageDescription</key><string>Relay your selected microphone to your paired computer and voice apps.</string><key>NSLocalNetworkUsageDescription</key><string>Connect to your paired Windows computer for audio.</string><key>LSMinimumSystemVersion</key><string>13.0</string></dict></plist>
PLIST
mkdir -p "$APP/Contents/Resources/licenses"
cp "$ROOT/third_party/licenses/"*.txt "$APP/Contents/Resources/licenses/"
cp "$ROOT/README.md" "$ROOT/VALIDATION.md" "$APP/Contents/Resources/"
codesign --force --deep --sign - "$APP"
echo "$APP"
