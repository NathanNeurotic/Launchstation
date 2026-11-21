# Lens: defects (bugs, security, performance, size and memory)

Your job on this unit is to find real defects and waste: categories 6, 5 and 4. A second reader covers dead code, duplication and overengineering on the same lines, so do not spend time there.

Read as the person who has to sign off that this code is safe on real hardware. For every function ask: where does each input come from (disc, memory card, USB, HDD, network, config file, user text), can any length, count or index be larger or smaller than the code assumes, and what happens on each failure.

## Bugs and security (6)

- Buffer overflows: sprintf, strcpy, strcat, memcpy, reads into fixed buffers, loop writes without a bound, off-by-one, strncpy without a terminator, snprintf return value used as a length, path joins without a length check.
- Integer problems: overflow in size arithmetic, signed/unsigned mixups, truncation (64-bit sizes or LBAs into 32 bits, 32 into 16), negative values used as lengths or indexes, division by zero.
- Unchecked results where failure is realistic and has a consequence: open/read/write/seek/malloc/memalign, fileXio and fio calls, SIF RPC calls, module loads, socket calls. Short reads and short writes treated as success.
- Leaks on error paths: file descriptors, memory, semaphores, threads, sockets, directory handles. Double free, use after free, use of uninitialised variables.
- Concurrency: data shared between threads, callbacks or interrupt handlers without protection; semaphore or mutex misuse; waiting with interrupts disabled; state changed under a running loop.
- PS2 specifics: unaligned access to packed data on MIPS; DMA and SIF RPC buffers that are not 64-byte aligned or live on the stack; missing cache flushes; large arrays on small thread stacks (IOP threads often have only a few KB); IOP memory exhaustion; wrong endianness in protocol or on-disc structures.
- Logic errors: wrong variable (copy-paste), inverted or always-true conditions, operator precedence, missing break, wrong loop bound, wrong error code, state that is not reset, wrong units.
- Untrusted input: anything parsed from files or the network (config, game lists, ELF and ISO headers, archives, SMB/NFS/UDPFS/HTTP replies, Lua values) that reaches a length, index, allocation or path without validation.
- PC code (PS2-Servers): path traversal out of the shared root, length fields from the client that are trusted, actions without authentication where the protocol expects it, exceptions swallowed so that a failure looks like success, sockets and files that are never closed, threads that are never joined, races on shared state, blocking calls on the GUI thread.

For every bug give the concrete trigger (the input or the sequence), show that the path is reachable (who calls it, with what data) and say what happens. If you cannot show reachability, lower the confidence and the rank, or drop it. No theoretical nitpicks.

## Performance (5)

- Blocking work on the UI or render thread: file, card, disc and network I/O, long loops, sleeps and fixed delays.
- Busy loops and polling without sleeping; repeated scans of the same directory or list; opening, stat-ing or parsing the same file again and again; quadratic work on lists that can be large (game lists).
- Per-frame waste: allocations, string formatting, texture uploads, full redraws when nothing changed.
- Slow start-up: modules loaded one after another with fixed waits, work that could be skipped.
- Needless memory-card or disk writes: saving unchanged config, writing on every change instead of once, rewriting a whole file for one value, tiny writes.
- Byte-at-a-time I/O and needless copies of large buffers.

Name the thread or context the code runs in and cite the call path.

## Size and memory (4)

- Oversized static buffers and tables (show the arithmetic), big arrays on the stack, fixed-size fields multiplied by large counts, tables that duplicate another table.
- Memory that is allocated and never freed, or held for the whole run when it is needed once.
- Embedded blobs, fonts, images, IRX modules and libraries that are linked or packed but not used, or packed twice.
- Strings and tables that could be shared, only where the saving is real.

Use `evidence/<Repo>/BUILD-EVIDENCE.md` and `evidence/<Repo>/raw/` for measured sizes when they exist.

## Fixes

Keep them minimal: the bound check that closes the overflow, the missing close, the call moved off the UI thread. A fix that adds a check or a test needs the `justification` field.
