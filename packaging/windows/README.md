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

Sign the generated installer in a protected release environment, using a timestamp server so the
signature remains valid after certificate rotation:

```powershell
signtool sign /fd SHA256 /td SHA256 /tr https://timestamp.digicert.com /a .\TierListMaker-*.exe
```

The `Platform Installers` GitHub Actions workflow installs NSIS and produces the same installer for
tags and manual releases. If `vkui` is private, configure a read-only `SUBMODULE_TOKEN` repository
secret.
