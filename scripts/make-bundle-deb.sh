#!/usr/bin/env bash
#
# make-bundle-deb.sh - самодостаточный .deb: приложение вместе с Qt внутри,
# без зависимостей от Qt-пакетов дистрибутива.
#
# Раскладка повторяет раскладку Qt, поэтому $ORIGIN-относительные RUNPATH самих
# библиотек Qt остаются верными и патчить ELF не нужно:
#
#   /opt/corvo/bin/Corvo          RPATH $ORIGIN/../lib   (-DCORVO_BUNDLE=ON)
#   /opt/corvo/bin/qt.conf        Prefix = ..
#   /opt/corvo/lib/*.so*          RUNPATH $ORIGIN
#   /opt/corvo/libexec/QtWebEngineProcess   RUNPATH $ORIGIN/../lib
#   /opt/corvo/plugins/*/*.so     RUNPATH $ORIGIN/../../lib
#   /opt/corvo/resources/         icudtl.dat, *.pak, v8_context_snapshot.bin
#   /opt/corvo/translations/      qtbase_*.qm + qtwebengine_locales/*.pak
#   /usr/bin/corvo -> /opt/corvo/bin/Corvo
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-bundle"
STAGE="$ROOT/build-bundle/stage"
DIST="$ROOT/dist"
OPT="opt/corvo"
QT="${CORVO_QT_ROOT:-$HOME/opt/Qt/6.11.1/gcc_64}"

die() { printf '\033[31mОшибка:\033[0m %s\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }

[[ -d "$QT/lib" ]] || die "Qt не найден в $QT (задайте CORVO_QT_ROOT)"
command -v dpkg-deb >/dev/null || die "нужен dpkg-deb"

VERSION="$(grep -m1 -Po '^\s+VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt")"
BUILDNO="$(grep -m1 -Po '^set\(CORVO_BUILD \K[0-9]+' "$ROOT/CMakeLists.txt")"
FULL="${VERSION}+${BUILDNO}"
APPID="$(grep -m1 -Po '^set\(CORVO_APP_ID "\K[^"]+' "$ROOT/CMakeLists.txt")"
DEB="$DIST/corvo_${FULL}_amd64.deb"

step "Corvo $FULL — самодостаточный пакет"
note "Qt: $QT"

# --------------------------------------------------------------------------
step "Сборка"
# --------------------------------------------------------------------------
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCORVO_BUNDLE=ON \
    -DCORVO_QT_ROOT="$QT" >/dev/null
cmake --build "$BUILD" >/dev/null
note "готово: $BUILD/Corvo"

# --------------------------------------------------------------------------
step "Раскладка пакета"
# --------------------------------------------------------------------------
rm -rf "$STAGE"
mkdir -p "$STAGE/$OPT"/{bin,lib,libexec,plugins,resources,translations} \
         "$STAGE/usr/bin" "$STAGE/DEBIAN"

# .desktop, иконки, metainfo и документация - в /usr, бинарник - в /opt/corvo.
DESTDIR="$STAGE" cmake --install "$BUILD" --prefix /usr --strip >/dev/null
mv "$STAGE/usr/bin/Corvo" "$STAGE/$OPT/bin/Corvo"
ln -sf "/$OPT/bin/Corvo" "$STAGE/usr/bin/corvo"
# Прежние версии ставили бинарник в /usr/bin/Corvo; ярлыки и записи автозапуска
# из них ссылаются на этот путь.
ln -sf "/$OPT/bin/Corvo" "$STAGE/usr/bin/Corvo"

cat > "$STAGE/$OPT/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
EOF

# Имя команды заменяется на путь установки. Аргументы сохраняются: у действия
# «Запустить в трее» это --hidden.
DESKTOP="$STAGE/usr/share/applications/$APPID.desktop"
sed -i -e "s|^Exec=Corvo|Exec=/$OPT/bin/Corvo|" \
       -e "s|^TryExec=Corvo|TryExec=/$OPT/bin/Corvo|" "$DESKTOP"
grep -q "^Exec=/$OPT/bin/Corvo --hidden$" "$DESKTOP" \
    || die ".desktop: действие «в трее» потеряло --hidden"
[[ $(grep -c "^Exec=/$OPT/bin/Corvo" "$DESKTOP") == 2 ]] \
    || die ".desktop: Exec подменился не во всех строках"
desktop-file-validate "$DESKTOP" || die ".desktop не проходит проверку"

# --------------------------------------------------------------------------
step "Библиотеки Qt (обход зависимостей)"
# --------------------------------------------------------------------------
# Плагины не видны через ldd, поэтому перечислены явно.
PLUGINS=(
    platforms/libqxcb.so
    platforms/libqwayland.so
    platformthemes/libqxdgdesktopportal.so
    xcbglintegrations/libqxcb-egl-integration.so
    xcbglintegrations/libqxcb-glx-integration.so
    wayland-decoration-client/libbradient.so
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
    wayland-graphics-integration-client/libdmabuf-server.so
    wayland-graphics-integration-client/libdrm-egl-server.so
    wayland-graphics-integration-client/libshm-emulation-server.so
    wayland-shell-integration/libxdg-shell.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    platforminputcontexts/libibusplatforminputcontextplugin.so
    imageformats/libqjpeg.so
    imageformats/libqgif.so
    imageformats/libqico.so
    imageformats/libqwebp.so
    imageformats/libqsvg.so
    iconengines/libqsvgicon.so
    tls/libqopensslbackend.so
    networkinformation/libqglib.so
    networkinformation/libqnetworkmanager.so
)

declare -A COPIED
# Обход начинается с бинарника из каталога сборки: его RPATH ведёт в Qt, поэтому
# ldd показывает настоящие библиотеки, а не системный Qt.
worklist=("$BUILD/Corvo" "$QT/libexec/QtWebEngineProcess")

install -m 755 "$QT/libexec/QtWebEngineProcess" "$STAGE/$OPT/libexec/"

for p in "${PLUGINS[@]}"; do
    # Без platforms/libqwayland.so программа не запустится в сеансе Wayland.
    [[ -f "$QT/plugins/$p" ]] || die "плагин не найден: $QT/plugins/$p"
    install -D -m 644 "$QT/plugins/$p" "$STAGE/$OPT/plugins/$p"
    worklist+=("$QT/plugins/$p")
done

# Транзитивное замыкание по оригиналам в каталоге Qt.
while ((${#worklist[@]})); do
    f="${worklist[0]}"; worklist=("${worklist[@]:1}")
    while read -r lib; do
        [[ -n "$lib" ]] || continue
        base="$(basename "$lib")"
        [[ -n "${COPIED[$base]:-}" ]] && continue
        COPIED[$base]=1
        cp -L "$lib" "$STAGE/$OPT/lib/$base"
        chmod 644 "$STAGE/$OPT/lib/$base"
        worklist+=("$lib")
    done < <(ldd "$f" 2>/dev/null | awk '/=> \//{print $3}' | grep -F "$QT/" || true)
done
note "скопировано библиотек Qt: ${#COPIED[@]}"

# --------------------------------------------------------------------------
step "Ресурсы WebEngine и переводы"
# --------------------------------------------------------------------------
for r in icudtl.dat v8_context_snapshot.bin \
         qtwebengine_resources.pak qtwebengine_resources_100p.pak \
         qtwebengine_resources_200p.pak; do
    [[ -f "$QT/resources/$r" ]] && install -m 644 "$QT/resources/$r" "$STAGE/$OPT/resources/"
done
# Инструменты разработчика (25 МБ) не нужны.

# Локали Chromium - все: язык WhatsApp приходит с телефона и может быть любым.
mkdir -p "$STAGE/$OPT/translations/qtwebengine_locales"
install -m 644 "$QT/translations/qtwebengine_locales"/*.pak \
    "$STAGE/$OPT/translations/qtwebengine_locales/"

# Переводы Qt - только языки, которые есть у Corvo.
for lang in ru en de uk; do
    for qm in "$QT/translations/qtbase_$lang.qm"; do
        [[ -f "$qm" ]] && install -m 644 "$qm" "$STAGE/$OPT/translations/"
    done
done
note "локалей Chromium: $(find "$STAGE/$OPT/translations/qtwebengine_locales" -name '*.pak' | wc -l)"

# --------------------------------------------------------------------------
step "Уменьшение размера"
# --------------------------------------------------------------------------
before=$(du -sm "$STAGE" | cut -f1)
find "$STAGE/$OPT" -type f \( -name '*.so' -o -name '*.so.*' \) -print0 |
    xargs -0 -r strip --strip-unneeded 2>/dev/null || true
strip --strip-unneeded "$STAGE/$OPT/libexec/QtWebEngineProcess" 2>/dev/null || true
after=$(du -sm "$STAGE" | cut -f1)
note "${before} МБ -> ${after} МБ"

# --------------------------------------------------------------------------
step "Проверка: всё ли нашлось внутри пакета"
# --------------------------------------------------------------------------
bad=0
checked=0
while read -r f; do
    out="$(ldd "$f" 2>/dev/null || true)"
    checked=$((checked + 1))

    if grep -q "not found" <<<"$out"; then
        printf '\033[31m  не найдено для %s:\033[0m\n%s\n' "${f#$STAGE}" \
            "$(grep 'not found' <<<"$out")"
        bad=1
    fi

    if grep -qF "$QT/" <<<"$out"; then
        printf '\033[31m  тянет Qt со сборочной машины: %s\033[0m\n' "${f#$STAGE}"
        bad=1
    fi

    # Библиотеки Qt обязаны разрешаться внутри пакета: путь в /usr/lib означает,
    # что библиотеку не упаковали.
    while read -r soname path; do
        case "$soname" in libQt6*|libicu*)
            if [[ "$path" != "$STAGE"* ]]; then
                printf '\033[31m  %s берёт %s из системы (%s) - не упакована\033[0m\n' \
                    "${f#$STAGE}" "$soname" "$path"
                bad=1
            fi ;;
        esac
    done < <(awk '/=> \//{print $1, $3}' <<<"$out")
done < <(find "$STAGE/$OPT" \( -type f -executable -o -name '*.so*' \) -print | sort -u)

((bad)) && die "пакет неполон, публиковать нельзя"
note "проверено файлов: $checked — все зависимости Qt внутри пакета"

# --------------------------------------------------------------------------
step "Зависимости пакета"
# --------------------------------------------------------------------------
# Вложенные библиотеки не принадлежат ни одному пакету - отсюда
# --ignore-missing-info.
cd "$ROOT"
mkdir -p debian
RAW="$(dpkg-shlibdeps -O --ignore-missing-info \
    -l"$STAGE/$OPT/lib" \
    "$STAGE/$OPT/bin/Corvo" \
    "$STAGE/$OPT/libexec/QtWebEngineProcess" \
    $(find "$STAGE/$OPT/plugins" -name '*.so') \
    $(find "$STAGE/$OPT/lib" -name '*.so*') 2>/dev/null \
    | sed 's/^shlibs:Depends=//')"
[[ -n "$RAW" ]] || die "dpkg-shlibdeps ничего не вернул"

# Зависимостей на Qt-пакеты дистрибутива быть не должно: libqt6widgets6 (>= 6.11)
# не существует ни в Debian 12, ни в Ubuntu 22.04, и apt отверг бы пакет целиком.
SHLIBS="$(tr ',' '\n' <<<"$RAW" | grep -v -E '^\s*libqt6' | paste -sd, - | sed 's/^ *//')"

# В Ubuntu 24.04 часть библиотек переименована из-за перехода на 64-битный
# time_t, и старое имя осталось только виртуальным пакетом. Добавляем новое имя
# как альтернативу - на Debian apt выберет первое из двух.
for pkg in libasound2 libglib2.0-0; do
    SHLIBS="$(sed -E "s/(^|, )$(sed 's/[.]/\\./g' <<<"$pkg")( \([^)]*\))?/\1$pkg\2 | ${pkg}t64/" <<<"$SHLIBS")"
done
DROPPED="$(tr ',' '\n' <<<"$RAW" | grep -E '^\s*libqt6' || true)"
[[ -n "$DROPPED" ]] && note "отброшены зависимости на системный Qt:$(tr '\n' ' ' <<<"$DROPPED")"
note "$SHLIBS"

# --------------------------------------------------------------------------
step "control, скрипты, apparmor"
# --------------------------------------------------------------------------
INSTALLED_KB=$(du -sk "$STAGE" | cut -f1)

cat > "$STAGE/DEBIAN/control" <<EOF
Package: corvo
Version: $FULL
Architecture: amd64
Maintainer: Serhii Artiushenko <artushenko@protonmail.com>
Installed-Size: $INSTALLED_KB
Depends: $SHLIBS
Recommends: pipewire | pulseaudio, xdg-desktop-portal, fonts-noto-color-emoji
Suggests: hunspell-ru, hunspell-en-us
Section: net
Priority: optional
Homepage: https://github.com/Artushenko-Sergei/Corvo
Description: Native WhatsApp Web client for Linux
 Corvo shows WhatsApp Web in a desktop window instead of a browser tab: a tray
 icon with an unread badge, desktop notifications, working voice and video
 calls, a persistent session so the QR code is scanned once, and autostart.
 .
 This package is self-contained - Qt 6.11 and Qt WebEngine are included in
 /opt/corvo, so no Qt packages are required from the distribution.
 .
 Corvo is an independent project, not affiliated with or endorsed by
 WhatsApp LLC or Meta Platforms, Inc.
EOF

# Ubuntu 24.04+ ограничивает user namespaces через AppArmor, а песочница
# Chromium их использует. Так же поступают пакеты Chrome и Chromium.
mkdir -p "$STAGE/etc/apparmor.d"
cat > "$STAGE/etc/apparmor.d/corvo" <<'EOF'
# Разрешение на непривилегированные user namespaces для песочницы Chromium.
# Нужно на Ubuntu 24.04 и новее; на других системах профиль безвреден.
abi <abi/4.0>,
include <tunables/global>

profile corvo /opt/corvo/bin/Corvo flags=(unconfined) {
  userns,
  include if exists <local/corvo>
}
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    [ -x "$(command -v update-desktop-database)" ] && update-desktop-database -q /usr/share/applications || true
    [ -x "$(command -v gtk-update-icon-cache)" ] && gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor || true
    # На Debian 12 парсер не знает abi/4.0 - это не ошибка установки.
    if [ -x /usr/sbin/apparmor_parser ] && [ -d /sys/kernel/security/apparmor ]; then
        /usr/sbin/apparmor_parser -r -T -W /etc/apparmor.d/corvo 2>/dev/null || true
    fi
fi
exit 0
EOF

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then
    [ -x "$(command -v update-desktop-database)" ] && update-desktop-database -q /usr/share/applications || true
    [ -x "$(command -v gtk-update-icon-cache)" ] && gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor || true
fi
exit 0
EOF

chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
echo "/etc/apparmor.d/corvo" > "$STAGE/DEBIAN/conffiles"

# md5sums для dpkg --verify.
( cd "$STAGE" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
    | xargs -0 md5sum > DEBIAN/md5sums )

# --------------------------------------------------------------------------
step "Сборка .deb"
# --------------------------------------------------------------------------
mkdir -p "$DIST"
rm -f "$DEB"
dpkg-deb --root-owner-group -Zxz -z9 --build "$STAGE" "$DEB" >/dev/null
note "$(du -h "$DEB" | cut -f1)  $DEB"

step "Готово"
dpkg-deb -I "$DEB" | sed -n '2,12p'
printf '\nУстановка:  sudo apt install %s\n' "$DEB"
