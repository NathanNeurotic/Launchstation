# Move: delete only confirmed copies

Implemented on the draft PR branch; not merged. NOT TESTED on hardware.

Move records source paths and stat metadata during Copy. Cleanup is permitted
only after the full selection copied and required verification passed. It
preflights every recorded source, then removes those files and nonempty-safe
directories in reverse traversal order. It never invokes recursive Delete.
Kept destinations are removed from the manifest and keep their cut source.

A reader with ambiguous end-of-directory (including BDM FAT/exFAT and MMCE)
cannot authorize a folder Move. Copy may complete, but Move reports that the
entire source selection was kept. Repeating the same partial listing does
not establish completeness. Ordinary Copy and explicit Delete retain their
existing behavior. A PSU restore records its source archive, not the entries
read from inside it.

The manifest is allocated only during Move, grows from 4 KiB, and stops at
256 KiB. Capacity/allocation failures keep the source. Stat failure or changed
available metadata also stops cleanup. New children survive because directory
removal is nonrecursive. Deletion is not transactional: a failure after it
begins can leave some verified entries moved and others retained. Metadata
checks do not provide filesystem locking against concurrent external writers.

Validation:
- Existing copy pipeline, listing callers, PSU backup/restore, complete listing,
  short-I/O and Get Size host suites pass.
- The new manifest suite passes with address/undefined-behavior sanitizers:
  changed/unseen entries, reverse traversal, unknown completeness, kept entries,
  allocation/cap failure and deletion failure. Peak manifest: 262144 bytes.
- Repeated identical short-listing integration keeps all five source files.
- Pinned local SDK: ps2build v2026.10.15, LLVM v0.3.11,
  sdk-core v2026.10.13.1, sdk-world v2026.10.28.
- Default LLVM target build passes after building the pinned Mbed TLS 3.6.7 and
  libjpeg-turbo 3.1.3 dependencies with that same LLVM SDK.
- Linked stack guard passes: Copy frame 4144/4352 bytes; HOST deepest checked
  chain 99792/106496, UDPFS 97152/106496. No invalid stack-realignment stores.
- Final-head CI and CodeRabbit review are separate gates.

A safe console acceptance uses disposable source/destination copies on each
backend, never the only copy of a save. It must verify retained source content,
destination read-back, incomplete-Move messaging and ordinary Copy/Delete.
