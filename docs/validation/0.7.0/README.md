# 0.7.0 — separate PDFs from images

Linux Release, Qt 6.4.2, PDFium 8125: 85 application cases passed at both 100% and 200% scale. All 23 updater cases also passed. The existing combined-PDF and export tests remain enabled.

New tests run the real conversion helper with synthetic JPG and PNG inputs, including Unicode and multi-dot filenames. They open every result with PDFium and check one page, dimensions and rendered image colors. Image-sized pages at 72/144 DPI and both A4 orientations are covered. Source bytes are checked for changes.

Preflight tests cover existing destinations, duplicate base names across formats, case-only duplicates and a destination that would overwrite an input. They verify rejection before the first output. A later missing input leaves earlier completed PDFs readable. Killing an active batch leaves completed PDFs readable, source images present and temporary partial files removable by the parent.

A UI test exercises File → PDF from images, the separate-output choice, output-folder selection and the completion message. The options dialog was also visually inspected. All conversion UI/worker strings have English catalogue entries. clang-format and git diff --check passed.

Windows build, installer and installed-client update to 0.7.0 require the manual Windows Release workflow and subsequent Windows verification. No Windows performance result is claimed.
