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

Windows packages intentionally omit the software OpenGL renderer, legacy D3D compiler, DXC, Qt
translations, and the VC redistributable installer. CI audits those exclusions and installs only the
five MSVC runtime DLLs imported by the application and deployed Qt libraries.
