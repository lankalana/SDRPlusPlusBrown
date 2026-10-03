#!/bin/sh
set -e

# ========================= Boilerplate =========================
BUILD_DIR=$1
BUNDLE=$2

source macos/bundle_utils.sh

# ========================= Prepare dotapp structure =========================

# Clear .app
rm -rf $BUNDLE

# Create .app structure
bundle_create_struct $BUNDLE

# Add resources
cp -R root/res/* $BUNDLE/Contents/Resources/

# Create the icon file
bundle_create_icns root/res/icons/sdrpp.macos.png $BUNDLE/Contents/Resources/sdrpp

# Create the property list
bundle_create_plist sdrpp SDR++Brown org.sdrpp.sdrppbrown 1.2.1 sdrp sdrpp sdrpp $BUNDLE/Contents/Info.plist

# ========================= Install binaries =========================

# Core
bundle_install_binary $BUNDLE $BUNDLE/Contents/MacOS $BUILD_DIR/sdrpp 
bundle_install_binary $BUNDLE $BUNDLE/Contents/Frameworks $BUILD_DIR/core/libsdrpp_core.dylib

# Install exactly the modules selected by CMake.
while IFS= read -r module_dir; do
    [ -n "$module_dir" ] || continue
    module_name=${module_dir##*/}
    bundle_install_binary "$BUNDLE" "$BUNDLE/Contents/Plugins" "$BUILD_DIR/$module_dir/$module_name.dylib"
done < "$BUILD_DIR/enabled-modules.txt"

# ========================= Finalize =========================

# Sign the app
bundle_sign $BUNDLE
