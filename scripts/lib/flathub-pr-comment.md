Sorry — my initial description replaced the template instead of filling in the
checklist, which is why the bot closed this. Here it is completed.

### Please confirm your submission meets all the criteria

- [X] Please describe the application briefly. **Corvo shows WhatsApp Web in a
  desktop window instead of a browser tab: a tray icon with an unread badge,
  desktop notifications, working voice and video calls, a session that is scanned
  once and then kept, and optional autostart. Built with Qt 6 and Qt WebEngine;
  only `web.whatsapp.com` may load in the main frame, every other link opens in
  the system browser. MIT licensed.**
- [X] Please attach a video showcasing the application on Linux using the Flatpak.
  **VIDEO_LINK** — the Flatpak build running on X11: the login screen, page zoom,
  the settings dialog, the about dialog and fullscreen.
- [X] The Flatpak ID follows all the rules listed in the
  [Application ID requirements](https://docs.flathub.org/docs/for-app-authors/requirements#application-id).
  `io.github.artushenko_sergei.Corvo` matches the GitHub account that hosts the
  source, https://github.com/Artushenko-Sergei, with the dash replaced by an
  underscore as required.
- [X] I have read and followed all the
  [Submission requirements](https://docs.flathub.org/docs/for-app-authors/requirements)
  and the [Submission guide](https://docs.flathub.org/docs/for-app-authors/submission)
  and I agree to them.
- [X] I am the author and developer of the project.
  **Link:** https://github.com/Artushenko-Sergei/Corvo

Notes for the reviewer:

- **QtWebEngine is not part of `org.kde.Platform`**, so the manifest uses
  `base: io.qt.qtwebengine.BaseApp` with `base-version` matching the runtime, and
  points `QTWEBENGINEPROCESS_PATH` at `/app/bin/QtWebEngineProcess`.
- **`--device=all`** is there for the camera in video calls: Flatpak has no
  camera-only device permission, and QtWebEngine does not use the camera portal,
  so `/dev/video*` has to be visible in the sandbox.
- **No `~/.config/autostart` access is requested.** The autostart switch is
  disabled in Flatpak builds — it belongs to the
  `org.freedesktop.portal.Background` portal, which the application does not
  implement yet.
- `flatpak-builder-lint manifest` reports no findings. Verified on both Wayland
  and X11.
- Corvo is an independent project, not affiliated with or endorsed by WhatsApp LLC
  or Meta Platforms, Inc. The application name and icon do not use their
  trademarks, and the metainfo states the lack of affiliation explicitly.
