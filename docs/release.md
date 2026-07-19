# Release Process

TierListMaker follows Semantic Versioning. Stable releases use tags such as `v0.2.0`; internal and
public test builds use prerelease tags such as `v0.2.0-beta.1`.

1. Release and tag QWindowKit and VkUI first. Dependency releases contain source only.
2. Update the TierListMaker submodules to those exact release commits.
3. Configure with `TLM_PACKAGE_VERSION` equal to the tag without its leading `v`.
4. Build and run the unit tests, then generate the platform installers with CPack.
5. Push an annotated tag. The `Platform Installers` workflow publishes checksums and
   `updates.json` with the GitHub Release.

The application reads the GitHub Releases API. Stable builds ignore prereleases; beta builds accept
both prerelease and stable releases and select the highest compatible Semantic Version. Downloads
are accepted only when the release asset reports a matching platform, architecture, size, and
SHA-256 digest.

When the selected release includes `updates.json`, the application reads optional release notes
from `localizations.en.changelog` and `localizations.zh_CN.changelog`, using the application
language (or the resolved system language). Missing or invalid localized metadata falls back to the
GitHub release body and never changes the package URL, size, or checksum selected from the release.

Every Windows release contains a full NSIS installer and a `WinUpdate` executable. The update
package carries only the signed application executable, verifies the installed `runtime-version`,
waits for the running application to exit, replaces it with rollback protection, and restarts it.
Clients use the compact package only when its runtime generation and GitHub asset metadata match;
otherwise they use the full installer. Bump `TLM_UPDATE_RUNTIME_VERSION` whenever Qt or the deployed
dynamic plugin/runtime set changes. macOS continues to update with the complete signed DMG.

The Windows NSIS installer installs machine-wide under `Program Files` by default. It intentionally
omits the software OpenGL renderer, legacy D3D compiler, DXC, Qt translations, and the VC
redistributable installer. CI audits those exclusions and installs only the five MSVC runtime DLLs
imported by the application and deployed Qt libraries.
