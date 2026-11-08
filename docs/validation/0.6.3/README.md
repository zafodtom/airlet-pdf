# 0.6.3 — native screen pixel presentation

The viewer previously drew a native-resolution tile back into a fractional logical page rectangle. The smooth-image flag used for previews also remained enabled for sharp tiles. This added a second image transformation after PDFium rasterization.

Sharp page rasters now use a viewport origin rounded to physical display pixels and an extent of raster pixels / devicePixelRatio. Request coverage and painting share this rectangle. Smooth image transformation is enabled only for the temporary preview, then disabled for sharp tiles. PDFium rendering flags, render resolution, cache limits, and worker scheduling policy are unchanged.

`sharpViewMatchesNativeRaster` compares actual viewport pixels with independently rendered native source tiles, at fractional zoom and multi-tile sizes in all four rotations. PDFium can round a vector edge differently when a tile translation is applied, so reference images use the same native tile boundaries rather than a separate full-page raster. The test checks presentation without resizing the reference and verifies that a repaint starts no new PDF jobs.

Before the fix, the fractional-0 test failed even after rendering finished. Full-suite results for normal and simulated 125%, 150%, and 200% scaling are recorded alongside this file. Tests use synthetic PDFs only. No user document has been copied into the test corpus.

These are Linux/offscreen checks with Qt 6.4.2 and pinned PDFium chromium/8125. Windows Qt 6.8.3 compilation, real monitor transitions, and the appearance of the reported user PDF must still be checked using the new Windows package. This validation does not claim identical text antialiasing to Adobe or a measured Windows performance result.
