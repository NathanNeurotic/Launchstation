# DEV9 transition follow-up after f4b621f

The #175 storage report includes a power-off crash, but that crash has not been
reproduced on hardware. This follow-up addresses two separate source-confirmed
failure paths found while auditing the same storage lifecycle. It does not
claim to resolve the reported crash or physical storage delays.

## Refused reset

`load_ps2dev9()` previously returned void. If the requested current/legacy DEV9
family differed from the resident family and the IOP reset was refused, callers
could accept `ps2dev9_loaded` from the old family and continue initializing
dependent modules. The production ATA regression reproduced this behavior before
the fix. Preserve the old resident state, return failure, and propagate it through
ATA, HDD, legacy ATAD, UDPFS, flash and IP loading. IP consumers also propagate the
result instead of consulting possibly stale IP/SMAP flags.

Power-off callback registration remains independent of optional DEV9 loading;
failure to load DEV9 must not prevent registration of the power-off callback.
CF ownership is retained and competing loads remain blocked.

## Successful reset and BDM dependencies

The ATA loader used to load BDM before resolving a possible DEV9 family reset.
A successful reset then cleared BDM before ATA_BD entry. Resolve DEV9 first and
load BDM in the resulting IOP session. Same-family reuse and the existing ATA
settle delay are preserved. No extra reset or timing reduction is introduced.

## Evidence and remaining limits

`tests/test_dev9_transition.py` compiles actual production loaders. It covers
refused family changes in both directions, successful reset/reload ordering,
same-family reuse, module/BDM failures, CF ownership, and rejection by HOST,
FTP, NETFS and dashboard-IP callers with old resident flags still set. Positive
network initialization and service loading are exercised too.

Existing CF-ownership, storage-transition, USB-lifecycle, power-off and native
cloud checks are retained. Standard/DS34 target builds are checked separately.
Actual DEV9 compatibility, storage latency, power-off behavior and wired/DS34
input recovery on physical PS2s remain acceptance gates.
