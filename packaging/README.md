# Packaging and releases

TierListMaker uses Qt's CMake deployment API to assemble a self-contained application and CPack to
create the platform-native installer:

- macOS: a drag-to-Applications DMG (`DragNDrop`).
- Windows: a per-user installer with Start Menu and uninstall integration (`NSIS`).

This keeps dependency discovery in Qt/CMake instead of duplicating `macdeployqt` and `windeployqt`
logic in shell scripts. The commands and signing details are documented in the platform folders.

## GitHub Actions

The `CI` workflow builds and tests on macOS and Windows. The `Platform Installers` workflow can be
run manually to produce workflow artifacts. Pushing a semantic version tag such as `v0.2.0` also
creates or updates the matching GitHub Release with both installers and SHA-256 checksum files.

Unsigned packages are useful for local testing. Public distribution should configure these GitHub
Actions secrets:

| Platform | Secret | Purpose |
| --- | --- | --- |
| macOS | `MACOS_CERTIFICATE_BASE64` | Base64-encoded Developer ID Application `.p12` |
| macOS | `MACOS_CERTIFICATE_PASSWORD` | Password for the `.p12` |
| macOS | `MACOS_SIGNING_IDENTITY` | Full Developer ID Application identity |
| macOS | `MACOS_NOTARY_KEY_BASE64` | Base64-encoded App Store Connect API `.p8` key |
| macOS | `MACOS_NOTARY_KEY_ID` | App Store Connect API key ID |
| macOS | `MACOS_NOTARY_ISSUER_ID` | App Store Connect issuer ID |
| Windows | `WINDOWS_CERTIFICATE_BASE64` | Base64-encoded Authenticode `.pfx` |
| Windows | `WINDOWS_CERTIFICATE_PASSWORD` | Password for the `.pfx` |

The workflow stores credentials only in the runner's temporary directory and removes them in an
always-run cleanup step. Certificates, private keys, and passwords must never be committed.
