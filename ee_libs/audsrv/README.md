# audsrv (ps2sdk world tier), vendored

`audsrv.h` and `libaudsrv.a` copied from the ps2dev `world/audsrv` package
(ps2sdk, LGPL-2.0). The SDK suite that ps2build resolves against mirrors
only its core packages, and classic PS2SDK layouts place audsrv elsewhere,
so the artifacts live here — the same reason `iop/__precompiled/audsrv.irx`
is vendored. Refresh all three from the same package version:

    cp $PS2DEV/packages/world/audsrv/include/audsrv.h  ee_libs/audsrv/include/
    cp $PS2DEV/packages/world/audsrv/lib/libaudsrv.a   ee_libs/audsrv/lib/
    cp $PS2DEV/packages/world/audsrv/bin/audsrv.irx    iop/__precompiled/
