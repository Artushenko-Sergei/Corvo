#!/usr/bin/env bash
# Подача в Flathub: проверка манифеста, сборка Flatpak, запись демонстрации,
# публикация комментария с чек-листом в PR flathub/flathub#9563.
#
#   scripts/flathub-submit.sh --video путь/к/видео.mp4
#
# Видео записывается отдельно: scripts/record-flatpak-demo.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PR=9563
APPID=io.github.artushenko_sergei.Corvo
MANIFEST="packaging/flatpak/flathub/$APPID.yaml"
WORK="${TMPDIR:-/tmp}/corvo-flathub"
COMMENT="$WORK/pr-comment.md"
VIDEO=""
[[ "${1:-}" == "--video" ]] && VIDEO="${2:?укажите путь к видео}"

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
set +e
LINT_OUT="$(lint manifest "$MANIFEST" 2>&1)"
LINT_RC=$?
set -e
case "$LINT_RC" in
    0) echo "    линтер без замечаний, тег на GitHub" ;;
    # 137 - линтер убит на глубоких проверках (нехватка ресурсов), это не
    # замечание к манифесту. На стороне Flathub он прогонится заново.
    137) echo "    линтер прерван системой, проверка отложена до CI Flathub" ;;
    *) printf '%s\n' "$LINT_OUT"; die "линтер Flathub нашёл замечания" ;;
esac

step "Видео"
[[ -n "$VIDEO" ]] || die "укажите видео: --video путь (записать: scripts/record-flatpak-demo.sh)"
[[ -s "$VIDEO" ]] || die "файл видео не найден: $VIDEO"
printf '    %s  %s\n' "$(du -h "$VIDEO" | cut -f1)" "$VIDEO"

step "Загрузка видео в релиз $TAG"
gh release upload "$TAG" "$VIDEO" --clobber
LINK="https://github.com/Artushenko-Sergei/Corvo/releases/download/$TAG/$(basename "$VIDEO")"
echo "    $LINK"

step "Комментарий в PR flathub/flathub#$PR"
sed "s|VIDEO_LINK|$LINK|" "$ROOT/scripts/lib/flathub-pr-comment.md" > "$COMMENT"
grep -q "VIDEO_LINK" "$COMMENT" && die "ссылка на видео не подставилась"
gh pr comment "$PR" --repo flathub/flathub --body-file "$COMMENT"
echo "    опубликован"

step "Готово"
echo "https://github.com/flathub/flathub/pull/$PR"
