# Airlet PDF — changelog

## 0.7.0 — separate PDFs from images

- Choose one combined PDF or a separate single-page PDF for every selected image.
- Separate PDFs retain each image's base name and use a chosen destination folder.
- Check all output names for collisions before converting; never overwrite existing files.
- Keep completed PDFs on cancellation/error, with a completion count; publish each file only after it is complete.

## 0.6.6 — remove an unnecessary update download

- Use GitHub's validated installer SHA-256 from release metadata directly; fetch the separate manifest only for older assets without a digest.
- Reject malformed nonempty asset digests.
- Identify the failed update stage and actual response host after redirects.
- Retry Qt transfer-timeout aborts while keeping explicit user cancellation final.

## 0.6.5 — update verification release

- Version-only maintenance release to verify the integrated update from 0.6.4.
- Application features, PDF rendering and updater behavior are unchanged.

## 0.6.4 — more reliable manual updates

- Fall back to GitHub's asset API for interrupted manifest and installer downloads; use bounded retries for temporary errors.
- Restart partial downloads cleanly, with HTTP status reporting and cancellation during retry delays.
- Exclude redirect/error response bodies from downloaded assets and preserve existing files.
- Publish a release only after all assets have uploaded successfully.

## 0.6.3 — sharper page display

- Present sharp PDF tiles at native screen resolution without a second scaling/filtering pass.
- Align raster placement to physical display pixels, including fractional display scaling.
- Retain fast previews and bounded tile caches.

## 0.6.2 — Trimenor brand alignment

- Developer/publisher identity standardized as **Trimenor** in About and the installer.
- User README reorganized into English and Czech instructions.
- Product version, Windows installer and update-package metadata synchronized.
- No changes to PDF rendering or application behavior.

## 0.6.1

- Default whole-page view and highlighted active view mode.
- Continue-between-files action available on the toolbar and in Settings.
- User documentation and About information revised; MIT license and third-party notices included.

See [release notes for 0.6.1](docs/releases/0.6.1.md) for details.
