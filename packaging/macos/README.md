# macOS Packaging

Configure and build a Release preset, then create the native drag-install DMG:

```sh
cmake --preset release
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -G DragNDrop
```

The install step runs Qt's deployment API before CPack creates the DMG. The generated image includes
the application and the standard `/Applications` symlink. Signing and notarization identities are
local/private and must never be committed.

For distribution outside local testing, configure
`-DTLM_MACOS_CODESIGN_IDENTITY="Developer ID Application: ..."`, then sign, submit, and staple the
DMG with credentials stored in the login keychain:

```sh
codesign --force --timestamp --sign "Developer ID Application: ..." TierListMaker-*.dmg
xcrun notarytool submit TierListMaker-*.dmg --keychain-profile TierListMaker --wait
xcrun stapler staple TierListMaker-*.dmg
```

The `Platform Installers` GitHub Actions workflow runs the same build for tags and manual releases.
If `vkui` is private, configure a read-only `SUBMODULE_TOKEN` repository secret.
