#!/usr/bin/env bash
# Linux/macOS: build, run pluginval at strictness 10, then the sonic tests for all five plugins.
set -e
cd "$(dirname "$0")/.."
cmake -B build -DARTIFACTS_TESTS=ON ${JUCE_DIR:+-DJUCE_DIR=$JUCE_DIR}
cmake --build build -j8
mkdir -p reports/data
for n in Console Kintsugi Duality Anemoia Equanimity; do
  ./build/${n}Tests_artefacts/Release/${n}Tests reports/data > reports/data/$n.tsv
done
if command -v pluginval >/dev/null; then
  for f in build/*_artefacts/Release/VST3/*.vst3; do pluginval --strictness-level 10 --validate-in-process --skip-gui-tests "$f"; done
fi
grep -hE "WARN|FAIL" reports/data/*.tsv || echo "no warnings"
