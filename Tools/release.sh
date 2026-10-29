#!/bin/bash
# Build, test, sign, copy into Сборки and install PowerComp (an effect: AU + VST3, no standalone app).
# Adapted from PowerBass/Tools/release_plugin.sh (which is for the instruments: auval aumu, Standalone).
#   macOS universal + Windows cross build; DSP and wrapper tests on arm64, x86_64 (Rosetta) and Wine;
#   ad hoc signing; Сборки/macOS, Сборки/Windows and the Windows zip; ~/Library/Audio/Plug-Ins (old copies to
#   the Trash); auval (both architectures, type aufx) and pluginval level 5. One summary line per step; stops on a failure.
#   bash Tools/release.sh "<label for the Trash folder>"
set -u
P=PowerComp; STAMP="${1:-$(date +%Y-%m-%d\ %H%M)}"
D="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(cd "$D/.." && pwd)"
C=~/Library/Caches/PowerKickTools/cmake/cmake/data/bin/cmake; W=~/Library/Caches/PowerToolsWin
export WIN_LLVM_ROOT=$W/llvm-mingw-20261020-ucrt-macos-universal WIN_SDK=$W/winsdk WIN_WINE=$W/wine-run.sh
BIN="$W/wine/Wine Stable.app/Contents/Resources/wine/bin"; U=~/Library/Caches/${P}BuildUniversal; BW=~/Library/Caches/${P}BuildWin
l=powercomp; O=~/Library/Caches/release-$P; mkdir -p "$O"
VER="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' "$D/CMakeLists.txt")"
fail() { echo "ОСТАНОВЛЕНО: $1"; exit 1; }
$C --build $U -j10 > "$O/mac.log" 2>&1 || fail "сборка Mac"; echo "сборка Mac: ок"
$C --build $BW -j10 > "$O/win.log" 2>&1 || fail "сборка Windows"; echo "сборка Windows: ок"
for t in ${l}_tests ${l}_wrapper_tests; do for a in arm64 x86_64; do
  (cd "$D" && arch -$a $U/${t}_artefacts/Release/$t > "$O/$t-$a.txt" 2>&1); r=$?; echo "$t $a: $(tail -1 "$O/$t-$a.txt")"; [ $r = 0 ] || fail "$t $a"
done; done
export WINEPREFIX=$W/wineprefix WINEDEBUG=-all
for t in ${l}_tests ${l}_wrapper_tests; do
  (cd "$D" && "$BIN/wine" $BW/${t}_artefacts/Release/$t.exe > "$O/$t-wine.txt" 2>/dev/null); r=$?; echo "$t wine: $(tr -d '\r' < "$O/$t-wine.txt" | grep -E 'PASS|FAIL' | tail -1)"; [ $r = 0 ] || fail "$t wine"
done; "$BIN/wineserver" -k 2>/dev/null
S="$D/Сборки"; Uo=$U/${P}_artefacts/Release; Wo=$BW/${P}_artefacts/Release; I=~/Library/Audio/Plug-Ins
for b in "$Uo/AU/$P.component" "$Uo/VST3/$P.vst3"; do codesign --force --deep --sign - "$b" 2>/dev/null; codesign --verify --deep --strict "$b" || fail "подпись $b"; done
T=~/.Trash/"$P до $STAMP"; old() { [ -e "$1" ] && mkdir -p "$2" && mv "$1" "$2/"; return 0; }
old "$S/macOS/$P.component" "$T/macOS"; old "$S/macOS/$P.vst3" "$T/macOS"; old "$S/Windows/$P.vst3" "$T/Windows"; for z in "$S/$P"-*-Windows-x64.zip; do old "$z" "$T"; done   # any version (1.1.0 left the 1.0.0 zip behind)
mkdir -p "$S/macOS" "$S/Windows"
ditto "$Uo/AU/$P.component" "$S/macOS/$P.component"; ditto "$Uo/VST3/$P.vst3" "$S/macOS/$P.vst3"; ditto "$Wo/VST3/$P.vst3" "$S/Windows/$P.vst3"
Z=$(mktemp -d); ditto "$S/Windows" "$Z/$P"; (cd "$Z" && zip -qr -X -D "$S/$P-$VER-Windows-x64.zip" "$P" -x "*.DS_Store"); rm -rf "$Z"
old "$I/Components/$P.component" "$T/Установленные"; old "$I/VST3/$P.vst3" "$T/Установленные"
mkdir -p "$I/Components" "$I/VST3"
ditto "$S/macOS/$P.component" "$I/Components/$P.component"; ditto "$S/macOS/$P.vst3" "$I/VST3/$P.vst3"
diff -rq "$I/Components/$P.component" "$S/macOS/$P.component" > /dev/null && diff -rq "$I/VST3/$P.vst3" "$S/macOS/$P.vst3" > /dev/null || fail "установка"
[ -d "$T" ] && echo "Сборки и установка: ок (прежние — в Корзине «$P до $STAMP»)" || echo "Сборки и установка: ок (прежних копий не было)"
killall -9 AudioComponentRegistrar 2>/dev/null
code=$(grep -m1 "PLUGIN_CODE" "$D/CMakeLists.txt" | awk '{print $2}')
for a in arm64 x86_64; do arch -$a auval -v aufx $code Mddy > "$O/auval-$a.txt" 2>&1; echo "auval $a: $(grep -E 'AU VALIDATION SUCCEEDED|FAILED' "$O/auval-$a.txt" | tail -1)"; done
PV="$ROOT/PowerBass/Исследования/2026-10-22 — Проверка задержки/Host/tools/pluginval-v1.0.4/pluginval.app/Contents/MacOS/pluginval"
for f in "VST3/$P.vst3" "Components/$P.component"; do "$PV" --strictness-level 5 --validate-in-process --output-dir "$O" "$I/$f" > "$O/pv-$(basename $f).txt" 2>&1; echo "pluginval $(basename $f): $(grep -E 'SUCCESS|FAILED' "$O/pv-$(basename $f).txt" | tail -1)"; done
echo ГОТОВО
