# Secure Ruwa updates

Ruwa uses small overlay patches. Signing adds only a JSON manifest and a small
detached signature; it does not turn the patch into a full application package.

The updater accepts an update only when all of these checks pass:

- the manifest, signature, redirects, and archive use HTTPS and exact
  allowlisted hosts;
- the detached CMS signature is valid and its certificate exactly matches the
  SHA-256 fingerprint compiled into Ruwa;
- the signed product, Windows x64 platform, Semantic Version, archive name,
  archive size, archive SHA-256, and configured size limit match;
- every extracted file has a signed source path, destination path, size, and
  SHA-256, with no path traversal, duplicate destinations, or extra files;
- the patch contains `Ruwa.exe`.

The application never executes content from the downloaded archive. The update
installer is a fixed PowerShell resource embedded in Ruwa. It stages a complete
candidate installation beside the current installation, applies the verified
overlay there, keeps the previous installation as a backup, and commits only
after the new application reaches its startup health check. A failed launch or
health timeout restores and restarts the previous installation.

## One-time signing-key setup

Choose a private location outside the repository and run this command yourself:

```powershell
& .\scripts\New-RuwaUpdateSigningKey.ps1 `
  -OutputPfxPath "D:\RuwaSecrets\ruwa-update-signing.pfx"
```

The script asks you to choose a password and creates:

- `ruwa-update-signing.pfx`: the private release key; keep it outside the
  repository, back it up securely, and never upload it;
- `ruwa-update-signing.cer`: the public certificate, safe to archive;
- `cmake/RuwaUpdateTrust.cmake`: only the public SHA-256 fingerprint compiled
  into Ruwa. Commit this file after reviewing it.

The updater is deliberately disabled at runtime when that fingerprint is empty
or invalid, even if `RUWA_ENABLE_UPDATES=ON`.

The private key is needed only when publishing an update. Users and build
machines do not need the PFX. Losing the PFX means existing installations cannot
trust updates signed by a replacement key; key rotation therefore requires a
normal release signed with the old key before switching the compiled
fingerprint.

## Create a signed patch

Prepare the same compact directory layout used by existing Ruwa updates. For
example:

```text
D:\RuwaUpdates\0.2.6-alpha\
|-- Main\Ruwa.exe
|-- effects\...
|-- shaders\...
```

`Main\` is stripped from installation destinations; other top-level paths are
kept. Then run:

```powershell
& .\scripts\New-RuwaUpdatePackage.ps1 `
  -Version 0.2.6-alpha `
  -SourceDirectory "D:\RuwaUpdates\0.2.6-alpha" `
  -SigningKeyPath "D:\RuwaSecrets\ruwa-update-signing.pfx" `
  -Description "Ruwa 0.2.6-alpha"
```

The command prompts for the PFX password, validates the patch, creates a ZIP,
generates its per-file signed manifest, signs the exact manifest bytes, verifies
the resulting signature, and checks that the PFX matches the fingerprint in
`cmake/RuwaUpdateTrust.cmake`.

Upload all three generated files to the matching version release in
`LuskusDeus/Ruwa-releases`:

- `Ruwa-0.2.6-alpha-win64.patch.zip`
- `latest.json`
- `latest.json.p7s`

The stable updater endpoint is the `latest.json` asset of the latest release;
the signed manifest points to the versioned patch asset. Never replace only one
of these files: publish the matching set together.

Before the first signed-patch release is published, the stable endpoint may
return `404`. A release is not complete until all three matching assets are
public and the stable `latest.json` URL returns the newly uploaded manifest.

Use `-DeletePath` only for installation-relative paths that must be removed by
the update. Deletions are signed and are applied to staging, never directly to
the running installation.

## Operational limits

The default compressed-archive and total extracted-file limit is 64 MiB,
comfortably above the current small Ruwa patches. The packaging script enforces
the same limit through `-MaximumPackageBytes`. Change it together with
`RUWA_UPDATE_MAX_ARCHIVE_BYTES` only when a legitimate signed patch needs more
room. Keep the host allowlist as small as possible; host names must be exact and
subdomains are not implicitly trusted.

Update activity and rollback errors are recorded in the application-local
`updates/last-update.log` file.
