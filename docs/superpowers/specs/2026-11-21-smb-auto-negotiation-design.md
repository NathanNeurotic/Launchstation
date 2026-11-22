# SMB automatic negotiation and live protocol status

## Intent

Make a saved SMB share connect using a dialect the server supports, while allowing an explicit setting to enforce one dialect. Show the protocol actually used on the Network (smb:/) row.

## Behavior

- Omitted `VERSION` means Automatic: negotiate SMB2/3 first, then try SMB1 only when the server cannot provide a supported SMB2/3 dialect.
- `VERSION = 1`, `2`, or `3` permits only that exact protocol/dialect. SMB2 and SMB3 enforcement must reach the driver; it is not merely a display label.
- Do not fall back for network, credentials, signing, or share-open errors; those retain their existing messages.
- Do not scan the network for servers or shares. Saved shares still connect on demand.
- Preserve mixed SMB1 and SMB2/3 configurations. The existing one-SMB1-session driver limit remains; another SMB1 share must fail rather than displace a connected one.
- The Network row's right side shows **No SMB Enabled**, **SMB1 Enabled**, **SMB2/3 Enabled**, or **SMB1 + SMB2/3 Enabled** from actual connected state, including fallback results.
- Existing saved paths and the iomanX-safe `smbold:` driver name remain supported.

## Approach

Track the declared protocol separately from the active protocol. Pass the requested exact dialect into smb2man's existing negotiation path, and add a narrowly scoped SMB1 fallback in the EE share-connect state machine for Automatic shares. Extend the existing share editor with Automatic and preserve explicit choices when saving. Compute the Network row label from connected shares' active protocols.

## Verification

Add host regressions for parser/editor round trips, exact-dialect request propagation, fallback only on protocol failure, no fallback on other errors, one-session protection, and all four status labels. Run the existing SMB suites, full host suite, and both packed LLVM builds. Verify the row in PCSX2 where possible; live SMB1/SMB2/SMB3 server negotiation remains distinct from emulator status evidence.
