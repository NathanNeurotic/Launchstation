# Lens: ported decoder code

These files are ports of upstream media decoders and decompressors (FFmpeg-derived code, libmpeg2, MAD, Tremor, liba52, libdca, xz and similar). Read every line, with these questions.

- Is it used? Which container or codec selects this decoder, and can a user reach it? Decoders, tables and functions that nothing calls, or that no file type selects, are category 1.
- `#if`/`#ifdef` branches for other platforms or configurations that this build never enables.
- Tables: large static tables; tables that duplicate one in another decoder (IDCT, VLC, window, quantiser, CRC); tables that are constant but not declared const; tables that are both computed at start-up and stored.
- Buffers: big static or stack buffers, per-stream allocations that are never freed.
- Robustness against broken or hostile media files: lengths, counts and indexes read from the stream that reach memcpy, an array index or an allocation without a check. Name the field and the path.
- Performance only where it is clear and cheap: a C path used where the repo already has a tuned routine, per-sample function calls in hot loops, needless copies.
- Local edits that look unfinished or wrong next to the surrounding upstream code.

Do not propose rewriting or restyling upstream algorithms. Prefer "stop compiling it", "delete the unused part", "share the table", "add the missing bound". Say that the code is upstream-derived.

Use categories 1, 2, 4, 5 and 6 as they apply.
