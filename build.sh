#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
APP="$HOME/Applications/CineCelu.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp Info.plist "$APP/Contents/Info.plist"
cp CineCelu.icns "$APP/Contents/Resources/CineCelu.icns"
swiftc -parse-as-library -O -swift-version 5 \
  -target arm64-apple-macos14.7 \
  -import-objc-header PhoneBridge.h \
  -o "$APP/Contents/MacOS/CineCelu" \
  PhoneBridge.c CineCeluApp.swift Library.swift ContentView.swift \
  -framework SwiftUI -framework AppKit -framework CoreFoundation \
  /System/Library/PrivateFrameworks/MobileDevice.framework/MobileDevice
codesign --force --sign - "$APP"
echo "BUILT $APP"
