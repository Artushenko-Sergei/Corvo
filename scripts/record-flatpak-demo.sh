#!/usr/bin/env bash
# Запись демонстрации Flatpak-версии для заявки в Flathub.
#
#   scripts/record-flatpak-demo.sh
#
# Собирает Flatpak из тега, ставит его, запускает под XWayland, пишет 40 секунд
# окна программы и после записи удаляет Flatpak-версию, чтобы её ярлык не
# оставался в меню рядом с пакетным.
#
# Во время записи щёлкайте по программе: меню «Файл -> Настройки», вкладки
# настроек, «Помощь -> О программе». Записывается только окно Corvo.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

APPID=io.github.artushenko_sergei.Corvo
MANIFEST="packaging/flatpak/flathub/$APPID.yaml"
BUILDDIR=build-flathub
OUT="$ROOT/dist/corvo-flatpak-demo.mp4"
SECONDS_TO_RECORD=40

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }

command -v ffmpeg >/dev/null || die "нужен ffmpeg: sudo apt install ffmpeg"
command -v xwininfo >/dev/null || die "нужен x11-utils: sudo apt install x11-utils"

step "Сборка Flatpak (10-15 минут)"
if [[ -f "$BUILDDIR/files/bin/Corvo" ]]; then
    echo "    готовая сборка найдена"
else
    flatpak run --filesystem="$ROOT" org.flatpak.Builder --jobs=4 --force-clean \
        --disable-rofiles-fuse "$BUILDDIR" "$MANIFEST"
    [[ -f "$BUILDDIR/files/bin/Corvo" ]] || die "сборка не дала /app/bin/Corvo"
fi

step "Установка (временно, удалится после записи)"
flatpak build-export "$ROOT/.demo-repo" "$BUILDDIR" master >/dev/null
flatpak remote-add --user --if-not-exists --no-gpg-verify corvo-demo "$ROOT/.demo-repo"
flatpak install --user -y --reinstall corvo-demo "$APPID" >/dev/null
trap 'flatpak uninstall --user -y "$APPID" >/dev/null 2>&1 || true
      flatpak remote-delete --user corvo-demo >/dev/null 2>&1 || true
      rm -rf "$ROOT/.demo-repo"
      echo "Flatpak-версия удалена, меню вернулось к пакетной"' EXIT

step "Запуск под X11"
flatpak run --env=QT_QPA_PLATFORM=xcb "$APPID" >"$ROOT/dist/demo-app.log" 2>&1 &
WID=""
for _ in $(seq 60); do
    WID=$(xwininfo -root -tree 2>/dev/null \
          | awk '/"Corvo": \("Corvo"/ && $2 ~ /[0-9]+x[0-9]+/ {print $1; exit}')
    [[ -n "$WID" ]] && break
    sleep 1
done
[[ -n "$WID" ]] || { tail -5 "$ROOT/dist/demo-app.log"; die "окно не появилось"; }

GEO=$(xwininfo -id "$WID")
W=$(awk '/Width:/{print $2}' <<<"$GEO")
H=$(awk '/Height:/{print $2}' <<<"$GEO")
X=$(awk '/Absolute upper-left X:/{print $4}' <<<"$GEO")
Y=$(awk '/Absolute upper-left Y:/{print $4}' <<<"$GEO")
W=$((W - W % 2)); H=$((H - H % 2))
echo "    окно $WID: ${W}x${H} в ${X},${Y}"

step "Запись ${SECONDS_TO_RECORD} секунд - щёлкайте по программе"
mkdir -p "$ROOT/dist"
ffmpeg -y -f x11grab -video_size "${W}x${H}" -framerate 15 -i ":0.0+${X},${Y}" \
       -t "$SECONDS_TO_RECORD" -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p \
       "$OUT" 2>&1 | tail -2 || die "ffmpeg не смог записать (попробуйте DISPLAY=:1)"

flatpak kill "$APPID" 2>/dev/null || true

step "Готово"
printf '%s\n' "$OUT"
ffprobe -v error -show_entries format=duration,size -of default=nw=1 "$OUT" 2>/dev/null
echo
echo "Дальше: scripts/flathub-submit.sh --video \"$OUT\""
