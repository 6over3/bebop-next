#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
xcode_contents_dir=$(dirname "$(xcode-select -p)")

plugins_dir="$HOME/Library/Developer/Xcode/Plug-ins"
spec_dir="${xcode_contents_dir}/SharedFrameworks/SourceModel.framework/Versions/A/Resources/LanguageSpecifications"
metadata_dir="${xcode_contents_dir}/SharedFrameworks/SourceModel.framework/Versions/A/Resources/LanguageMetadata"

mkdir -p "$plugins_dir" "$spec_dir" "$metadata_dir"

cp -r "$script_dir/Bebop.ideplugin" "$plugins_dir/"
cp "$script_dir/Bebop.xclangspec" "$spec_dir/"
cp "$script_dir/Xcode.SourceCodeLanguage.Bebop.plist" "$metadata_dir/"

echo 'Installed. Restart Xcode and click "Load bundle" when prompted.'
