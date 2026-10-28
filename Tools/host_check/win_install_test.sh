#!/bin/bash
# Installs PowerComp-<ver>-Windows-x64-Setup.exe silently into a fresh Wine prefix (a clean "Windows"), checks the
# files and the Apps & features entry, loads the installed VST3 with host_check (a VST3 effect host), uninstalls and
# checks that nothing is left. Adapted from PowerBass/Tools/host_check/win_install_test.sh (no standalone app here).
#   bash Tools/host_check/win_install_test.sh
# Needs: the installer in Установщики, host_check built for Windows in ~/Library/Caches/HostCheckEffectWin
# (see CMakeLists.txt here), Wine in ~/Library/Caches/PowerToolsWin. The previous test prefix goes to the Trash.
set -u
P=PowerComp; D="$(cd "$(dirname "$0")/../.." && pwd)"
VER="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' "$D/CMakeLists.txt")"
W=~/Library/Caches/PowerToolsWin; BIN="$W/wine/Wine Stable.app/Contents/Resources/wine/bin"
export WINEPREFIX=$W/prefix-install-$P WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
SETUP="$D/Установщики/$P-$VER-Windows-x64-Setup.exe"; SRC="$D/Сборки/Windows"
HC=~/Library/Caches/HostCheckEffectWin/host_check_artefacts/Release/host_check.exe
C="$WINEPREFIX/drive_c"; VST="$C/Program Files/Common Files/VST3/$P.vst3"; APPDIR="$C/Program Files/MyDarknessDays/$P"
KEY='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\MyDarknessDays '"$P"
timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }  # no coreutils timeout on macOS
ok() { if eval "$2"; then echo "OK   $1"; else echo "FAIL $1"; fi; }
[ -d "$WINEPREFIX" ] && mv "$WINEPREFIX" ~/.Trash/"wine prefix-install-$P $(date +%H%M%S)"
"$BIN/wineboot" -i > /dev/null 2>&1; "$BIN/wineserver" -w
ok "до установки плагина нет" '[ ! -e "$VST" ]'
cp "$SETUP" "$C/setup.exe"
timeout 300 "$BIN/wine" 'C:\setup.exe' /S > /dev/null 2>&1; echo "установщик: код $?"; "$BIN/wineserver" -w
ok "VST3 в Common Files\\VST3 совпадает со Сборки/Windows" 'diff -rq "$VST" "$SRC/$P.vst3" > /dev/null'
ok "деинсталлятор на месте" '[ -f "$APPDIR/Uninstall $P.exe" ]'
"$BIN/wine" reg query "$KEY" /reg:64 2>/dev/null | tr -d '\r' | grep -E "DisplayName|DisplayVersion|Publisher|UninstallString" | sed 's/^/     /'
ok "запись в «Приложения и возможности»" '"$BIN/wine" reg query "$KEY" /reg:64 > /dev/null 2>&1'
echo "== хост VST3 загружает установленный плагин"
cp "$HC" "$C/host_check.exe"
timeout 600 "$BIN/wine" 'C:\host_check.exe' "C:\\Program Files\\Common Files\\VST3\\$P.vst3" 2>/dev/null | tr -d '\r'; echo "host_check: код ${PIPESTATUS[0]}"
"$BIN/wineserver" -w
echo "== удаление"
"$BIN/wine" "C:\\Program Files\\MyDarknessDays\\$P\\Uninstall $P.exe" /S > /dev/null 2>&1; "$BIN/wineserver" -w; sleep 2; "$BIN/wineserver" -w
ok "VST3 удалён" '[ ! -e "$VST" ]'
ok "папка деинсталлятора удалена" '[ ! -e "$APPDIR" ] && [ ! -e "$C/Program Files/MyDarknessDays" ]'
ok "запись в реестре удалена" '! "$BIN/wine" reg query "$KEY" /reg:64 > /dev/null 2>&1'
"$BIN/wineserver" -k 2>/dev/null
echo ГОТОВО
