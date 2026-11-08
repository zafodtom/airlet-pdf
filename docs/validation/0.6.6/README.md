# 0.6.6 — remove redundant manifest and fix transfer-timeout retries

Linux Release / Qt 6.4.2: 74 application cases and 23 updater transport/dialog cases passed. The updater dialog is tested with an injected synthetic network manager using its production callbacks. Verified cases include an offer after just the release metadata request, download and SHA-256 success, rejection/removal of corrupted bytes without enabling installation, and the legacy manifest route when GitHub supplies no digest. Existing transport cancellation, TLS, size-limit and fallback checks remain enabled.

The reported 0.6.4-to-0.6.5 failure was reproduced with the original Qt path: release metadata returned HTTP 200, the mandatory manifest request returned 503 through the browser URL and 504 through the API. HTTP/1.1 did not establish a reliable fix; a parallel identity/default encoding comparison returned 503 for both. Neither speculative workaround was added to production.

The original UI did not identify the failing stage, and reported the initial host even after a redirect. New errors identify the operation and actual response host. A test checks that a CDN error is attributed to the CDN rather than the original github.com address. The screenshot alone cannot identify the user's failed stage or exact upstream cause.

GitHub already supplies the expected installer name, size and SHA-256 in the HTTPS release metadata. With a valid digest the separate manifest adds no integrity information. The new dialog skips that request, rejects malformed nonempty digests, and retains the manifest path for legacy assets without a digest. Download size/hash and the native helper's second hash check remain mandatory.

A subsequent live dialog check offered the update correctly but failed during the installer transfer with Qt error 5. Qt 6.8.3 source confirms that the transfer inactivity timer calls abort(), producing OperationCanceledError rather than necessarily TimeoutError. The previous retry whitelist omitted it. Active code-5 failures now use bounded retries; explicit cancellation disconnects the reply before aborting it, and policy/I/O/size aborts remain terminal. Tests verify the timeout-style abort retry separately from user cancellation. Closed replies are not read during completion.

Pinned primary source: https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/network/access/qnetworkreplyhttpimpl.cpp

After these changes, a live check using the actual UpdateDialog offered immutable release 0.6.5 from metadata, downloaded its 62,102,358-byte installer, and reached the verified/restart button. SHA-256 was checked by the production dialog against the release digest (4b3ca87860606897125e9c47272810b82b924b7fafcd986ea0455213e5e0c197). The probe exited before confirmation/installation, and its temporary directory was removed.

This verifies transport and dialog behavior on Linux, not a completed Windows installation. Windows Qt 6.8.3 build/package verification and the user's installed-client transition remain pending. GitHub/API/CDN availability cannot be guaranteed by client code; an old installed client must first obtain the new patch, potentially via a one-time manual installer if its own download fails.
