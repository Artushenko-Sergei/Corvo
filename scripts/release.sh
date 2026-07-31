#!/usr/bin/env bash
# Выпуск: поднять версию, собрать пакет, поставить тег, опубликовать релиз.
#
#   scripts/release.sh build|patch|minor|major "описание изменения"
#
# Повторный запуск после сбоя безопасен: существующий тег и релиз не трогаются.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

KIND="${1:?укажите build|patch|minor|major}"
NOTE="${2:?укажите описание изменения}"

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }

step "Что войдёт в выпуск"
git status --short || true

step "Версия"
scripts/bump-version.sh "$KIND" "$NOTE"
VERSION="$(grep -m1 -Po '^\s+VERSION \K[0-9.]+' CMakeLists.txt)"
BUILDNO="$(grep -m1 -Po '^set\(CORVO_BUILD \K[0-9]+' CMakeLists.txt)"
FULL="${VERSION}+${BUILDNO}"
TAG="v${VERSION}"
DEB="dist/corvo_${FULL}_amd64.deb"

step "Запись о выпуске в AppStream"
META=packaging/io.github.artushenko_sergei.Corvo.metainfo.xml
python3 - "$META" "$VERSION" "$NOTE" <<'PY'
import re, sys, datetime, pathlib
path, version, note = sys.argv[1], sys.argv[2], sys.argv[3]
p = pathlib.Path(path)
s = p.read_text()
if f'<release version="{version}"' in s:
    print("запись уже есть"); sys.exit(0)
today = datetime.date.today().isoformat()
entry = (f'    <release version="{version}" date="{today}">\n'
         f'      <description>\n        <p>{note}</p>\n      </description>\n'
         f'    </release>\n')
s = s.replace("  <releases>\n", "  <releases>\n" + entry, 1)
p.write_text(s)
print(f"добавлена запись {version} ({today})")
PY
appstreamcli validate "$META" >/dev/null || die "метаданные AppStream не проходят проверку"

step "Пакет (сборка и сжатие занимают несколько минут)"
rm -f dist/*.deb dist/SHA256SUMS.txt 2>/dev/null || true
# Предупреждение Qt про qml-плагин Qt PDF к делу не относится и только мешает
# читать вывод.
scripts/make-bundle-deb.sh 2>&1 | grep -vE \
    "CMake Warning|qml plugin|Call Stack|cmake_language|Qt6(Core|Qml)Macros|:DEFERRED|^  "
[[ -f "$DEB" ]] || die "пакет не собрался: $DEB"
sha256sum "$DEB" > dist/SHA256SUMS.txt
printf '    %s  %s\n' "$(du -h "$DEB" | cut -f1)" "$DEB"

step "Фиксация и тег"
git add -A
git commit -q -m "Версия ${FULL}: ${NOTE}"
git push -q origin main
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "    тег $TAG уже существует, пропущен"
else
    git tag -a "$TAG" -m "Corvo ${VERSION}"
    git push -q origin "$TAG"
    echo "    тег $TAG"
fi

step "Релиз на GitHub"
if gh release view "$TAG" >/dev/null 2>&1; then
    gh release upload "$TAG" "$DEB" dist/SHA256SUMS.txt --clobber
    echo "    файлы обновлены в существующем релизе $TAG"
else
    gh release create "$TAG" --title "Corvo ${VERSION}" --notes "$NOTE" \
        "${DEB}#Corvo для Debian и Ubuntu (64-бит)" \
        "dist/SHA256SUMS.txt#Контрольная сумма"
fi

step "Готово"
printf 'Corvo %s\nУстановка: sudo apt install %s/%s\n' "$FULL" "$ROOT" "$DEB"
