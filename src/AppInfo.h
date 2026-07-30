// AppInfo.h - identity and version constants injected by the build system.
#ifndef CORVO_APPINFO_H
#define CORVO_APPINFO_H

// CMake passes these as compile definitions (see CMakeLists.txt). The fallbacks
// only matter for tools that parse the sources without the project's flags -
// IDEs, linters and standalone test builds - so every translation unit that uses
// them can rely on them being defined.
#ifndef CORVO_APP_ID
#  define CORVO_APP_ID "io.github.artushenko_sergei.Corvo"
#endif
#ifndef CORVO_VERSION
#  define CORVO_VERSION "0.0.0"
#endif
#ifndef CORVO_BUILD
#  define CORVO_BUILD 0
#endif
#ifndef CORVO_VERSION_FULL
#  define CORVO_VERSION_FULL CORVO_VERSION
#endif

#endif // CORVO_APPINFO_H
