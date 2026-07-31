#!/usr/bin/env bash
# Подача в Flathub: проверка манифеста, сборка Flatpak, запись демонстрации,
# публикация комментария с чек-листом в PR flathub/flathub#9563.
#
#   scripts/flathub-submit.sh [--skip-video]
#
# Видео пишется на отдельном экране :99, где нет других окон, и приложение
# запускается из каталога сборки (flatpak-builder --run) - в систему оно не
# устанавливается и ярлыков не создаёт.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PR=9563
APPID=io.github.artushenko_sergei.Corvo
MANIFEST="packaging/flatpak/flathub/$APPID.yaml"
BUILDDIR=build-flathub
WORK="${TMPDIR:-/tmp}/corvo-flathub"
VIDEO="$WORK/corvo-flatpak-demo.mp4"
COMMENT="$WORK/pr-comment.md"
SKIP_VIDEO=0
[[ "${1:-}" == "--skip-video" ]] && SKIP_VIDEO=1

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }
lint() { flatpak run --command=flatpak-builder-lint --filesystem="$ROOT" org.flatpak.Builder "$@"; }

mkdir -p "$WORK"
TAG="$(grep -m1 -Po '^\s+tag: \K\S+' "$MANIFEST")"
COMMIT="$(grep -m1 -Po '^\s+commit: \K\S+' "$MANIFEST")"

step "Манифест: $TAG / ${COMMIT:0:8}"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null || die "тега $TAG нет"
[[ "$(git rev-parse "$TAG^{commit}")" == "$COMMIT" ]] || die "commit в манифесте не совпадает с тегом $TAG"
git ls-remote --exit-code --tags origin "$TAG" >/dev/null || die "тег $TAG не отправлен на GitHub"
lint manifest "$MANIFEST" || die "линтер Flathub нашёл замечания"
echo "    линтер без замечаний, тег на GitHub"

step "Сборка Flatpak из тега"
if [[ -f "$BUILDDIR/files/bin/Corvo" ]]; then
    echo "    готовая сборка найдена, пропускаю"
else
    flatpak run --filesystem="$ROOT" org.flatpak.Builder --jobs=2 --force-clean \
        --disable-rofiles-fuse "$BUILDDIR" "$MANIFEST" 2>&1 | tail -4
    [[ -f "$BUILDDIR/files/bin/Corvo" ]] || die "сборка не дала /app/bin/Corvo"
fi

if ((SKIP_VIDEO)); then
    step "Видео пропущено по ключу --skip-video"
else
    step "Запись демонстрации"
    command -v Xvfb >/dev/null || die "нужен Xvfb"
    command -v ffmpeg >/dev/null || die "нужен ffmpeg"
    [[ -x "$WORK/xvenv/bin/python" ]] || {
        python3 -m venv "$WORK/xvenv"
        "$WORK/xvenv/bin/pip" install -q python-xlib
    }
    cp "$ROOT/scripts/lib/drive-demo.py" "$WORK/drive.py"

    pkill Xvfb 2>/dev/null || true
    sleep 1
    Xvfb :99 -screen 0 1280x800x24 -listen tcp -ac >"$WORK/xvfb.log" 2>&1 &
    XV=$!
    trap 'kill $XV 2>/dev/null || true; pkill -f "flatpak-builder --run" 2>/dev/null || true' EXIT
    for _ in $(seq 20); do xdpyinfo -display :99 >/dev/null 2>&1 && break; sleep 1; done

    DISPLAY=localhost:99 QT_QPA_PLATFORM=xcb \
        flatpak-builder --run "$BUILDDIR" "$MANIFEST" /app/bin/Corvo >"$WORK/app.log" 2>&1 &

    WID=""
    for _ in $(seq 90); do
        WID=$(DISPLAY=:99 xwininfo -root -tree 2>/dev/null | awk '/"Corvo": \("Corvo"/ {print $1; exit}')
        [[ -n "$WID" ]] && break
        sleep 1
    done
    [[ -n "$WID" ]] || { tail -5 "$WORK/app.log"; die "окно приложения не появилось"; }
    for _ in $(seq 60); do grep -q "Page loaded" "$WORK/app.log" && break; sleep 1; done
    sleep 3

    ffmpeg -y -f x11grab -video_size 1280x800 -framerate 15 -i :99 -t 50 \
           -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p "$VIDEO" \
           >"$WORK/ffmpeg.log" 2>&1 &
    FF=$!
    sleep 1
    "$WORK/xvenv/bin/python" "$WORK/drive.py" || echo "    нажатия завершились с ошибкой, запись продолжается"
    wait "$FF"
    [[ -s "$VIDEO" ]] || die "видео не записалось"
    printf '    %s  %s\n' "$(du -h "$VIDEO" | cut -f1)" "$VIDEO"
fi

step "Загрузка видео в релиз $TAG"
if ((SKIP_VIDEO)); then
    LINK="(видео не приложено)"
else
    gh release upload "$TAG" "$VIDEO" --clobber
    LINK="https://github.com/Artushenko-Sergei/Corvo/releases/download/$TAG/$(basename "$VIDEO")"
    echo "    $LINK"
fi

step "Комментарий в PR flathub/flathub#$PR"
sed "s|VIDEO_LINK|$LINK|" "$ROOT/scripts/lib/flathub-pr-comment.md" > "$COMMENT"
grep -q "VIDEO_LINK" "$COMMENT" && die "ссылка на видео не подставилась"
gh pr comment "$PR" --repo flathub/flathub --body-file "$COMMENT"
echo "    опубликован"

step "Готово"
echo "https://github.com/flathub/flathub/pull/$PR"
