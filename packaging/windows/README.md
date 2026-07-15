# Windows Packaging

From an MSVC developer shell with NSIS 3.03 or newer installed:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
cpack --config build/windows-release/CPackConfig.cmake -G NSIS
```

The install step runs Qt's deployment API before CPack creates a DPI-aware per-user installer. It
adds Start Menu and Apps & Features entries, supports clean upgrades/uninstall, and does not modify
`PATH`. Code-signing certificates and passwords are local/private and must never be committed.

Sign both the application binary and generated installer in a protected release environment, using
a timestamp server so the signatures remain valid after certificate rotation:

```powershell
signtool sign /fd SHA256 /td SHA256 /tr https://timestamp.digicert.com /a \
  .\build\windows-release\TierListMaker.exe
cpack --config build/windows-release/CPackConfig.cmake -G NSIS
signtool sign /fd SHA256 /td SHA256 /tr https://timestamp.digicert.com /a \
  .\TierListMaker-*.exe
```

The `Platform Installers` workflow installs NSIS and produces the same installer for tags and manual
dispatches. With the Authenticode secrets listed in the parent packaging README, it signs the app
before packaging and signs the final installer. A `vX.Y.Z` tag publishes the signed installer and
its SHA-256 checksum to the matching GitHub Release.
