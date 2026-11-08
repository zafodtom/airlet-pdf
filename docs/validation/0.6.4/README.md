# 0.6.4 — updater review and verification

Linux Release with Qt 6.4.2: 74 application test cases and 18 downloader test cases passed. Downloader tests inject synthetic QNetworkReply responses into the same production UpdateDownload implementation; they do not use the internet or run an installer.

Coverage includes 504-to-API fallback, direct-route 403/404 fallback without repeating an API 403, correct JSON/octet-stream headers, bounded retries, a partially received installer followed by a clean restart and correct final SHA-256, redirect-body exclusion, blocked insecure redirects, TLS failure without retry, cancellation during a retry delay, partial-file cleanup, size limits in memory and file modes, preservation of existing files, and restart after failure. Application tests additionally reject API URLs outside the intended repository or with invalid paths/queries.

Manual read-only network probes using the actual Qt downloader also succeeded against the immutable v0.6.3 release:

- Manifest: after the primary route failed, fallback to asset API 723531534 returned 217 bytes with the expected version and installer hash.
- Installer: after the primary route failed, fallback to asset API 723531529 streamed 62,098,073 bytes to a temporary file. SHA-256 matched 4206c8ed3d630bd525be0399f431b84ca82fc8dc329acc0839c31332718ca135. The downloaded file was deleted; it was never executed.

The observed direct manifest URL returned HTTP 504 in the diagnostic environment. The old user dialog omitted its HTTP status, so this cannot establish the exact response received on the user's PC. The preceding successful 0.6.1-to-0.6.2 update remains valid evidence that the updater can work normally.

Release publication was reviewed and changed from immediate publication to draft/upload/publish. This avoids exposing incomplete assets; there is no evidence that this timing caused the reported failure several minutes after publication.

The viewer still has no QtNetwork dependency. No startup or periodic network checks were introduced. The native installer handoff and its second SHA-256 check were reviewed and left intact.

Remaining verification: Windows Qt 6.8.3 CI/package creation, publication via the new draft workflow, and installation from the user's existing copy. This does not claim a completed Windows update to 0.6.4.
