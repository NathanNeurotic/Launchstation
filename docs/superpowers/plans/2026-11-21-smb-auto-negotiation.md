# SMB Automatic Negotiation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Automatically negotiate SMB2/3 then SMB1 for saved shares, allow exact VERSION enforcement, and display the protocol actually connected.

**Architecture:** Keep declared and active protocols separate in `SmbShare`. Extend the existing smb2man connect request with the requested dialect, add a protocol-error-only fallback in the EE connector, and derive the Network row from active connections.

**Tech Stack:** C EE/IOP, existing SMB parsers/drivers, Python/C host harnesses, ps2build LLVM, PCSX2.

**Spec:** `docs/superpowers/specs/2026-11-21-smb-auto-negotiation-design.md`

## Global Constraints

- Omitted VERSION = Automatic; explicit 1/2/3 permits only that dialect.
- Fall back only on SMB2/3 protocol negotiation failure, never network/login/signing/share errors.
- No network discovery and no second SMB1 session; preserve mixed configurations and saved paths.
- Status labels: No SMB Enabled, SMB1 Enabled, SMB2/3 Enabled, SMB1 + SMB2/3 Enabled.

## Review Focus

- A protocol failure must not leave a half-open SMB2 session before SMB1 starts; test cleanup/order in Task 3.
- Automatic fallback must route subsequent file access through SMB1, not merely relabel the share; test active-path selection in Task 3.
- Explicit SMB2/SMB3 must not silently negotiate the other family; test driver request selection in Task 2.
- A failed fallback must preserve the useful original/fallback error and retry cleanly; test state/backoff in Task 3.
- Status must reflect active, not merely configured, protocols; test all four labels in Task 4.

---

### Task 1: Configuration model and editor

**Files:**
- Modify: `include/smb_shares.h`, `src/smb_cnf.c`, `src/dash_settings.c`
- Test: `tests/test_smb_shares.py`, `tests/test_smb_cnf_edit.py`, `tests/test_smb_settings_row.py`

**Interfaces:**
- Produces: `SmbShare.proto_version`: 0 = Automatic, 1/2/3 = exact declared choice; `SmbShare.active_version`: 0 until connected, then 1 or 2.
- Produces: editor kind 0 = Automatic, 1/2/3 = exact choice, 4 = NFS.

- [x] **Step 1: Write failing parser/serializer/editor tests**

Assert omitted VERSION parses as proto 0, each explicit value round-trips exactly, Automatic is the new-share default, and every editor choice maps to/from kind 0/1/2/3 without changing NFS.

- [x] **Step 2: Run tests to verify they fail**

Run the three listed tests. Expected: omitted VERSION currently records proto 2 and Automatic is absent.

- [x] **Step 3: Implement the model and editor**

Default proto_version to 0, preserve explicit 1/2/3, initialize active_version to 0, and add Automatic to the editor without changing existing config block formatting except explicit VERSION lines.

- [x] **Step 4: Run tests to verify they pass**

Run the three listed tests; all PASS.

### Task 2: Exact dialects in smb2man

**Files:**
- Modify: `include/smb2man.h`, `iop/smb2man/smb2man.c`
- Test: `tests/test_smb_explicit_port.py`, `tests/test_smb2_dialects.py`

**Interfaces:**
- Consumes: proto_version 0/2/3 from Task 1.
- Produces: `smb2Connect_in_t.protocol` (0 automatic, 2 SMB2-only, 3 SMB3-only), built together on EE/IOP.

- [x] **Step 1: Write failing request/negotiation tests**

Extend the EE harness to assert protocol propagation. Add a host harness for the driver's negotiation selection: protocol 2 makes only the 2.0.2/2.1 offer, 3 makes only 3.0/3.0.2, and 0 preserves the existing two-stage sequence.

- [x] **Step 2: Run tests to verify they fail**

Run both listed tests. Expected: protocol field and selection are absent.

- [x] **Step 3: Implement exact negotiation**

Validate protocol on connect, store it in the selected share, and choose the existing negotiation calls accordingly. Do not alter signing/login behavior or wire dialect values.

- [x] **Step 4: Run tests to verify they pass**

Run both listed tests; all PASS.

### Task 3: Automatic SMB1 fallback and active routing

**Files:**
- Modify: `src/smb_shares.c`, `src/dash_browser.c`
- Test: `tests/test_smb_connect_state.py`, `tests/test_smb_root_read.py`

**Interfaces:**
- Consumes: declared/active fields from Task 1 and `-SMB2_CONNECT_ERR_PROT` from Task 2.

- [x] **Step 1: Write failing fallback/routing tests**

Mock the two connectors. Prove Automatic falls back only for PROT, records active_version=1 on SMB1 success, uses the SMB1 path thereafter, and leaves other errors untouched. Prove explicit shares never fall back, mixed connections survive, and a second SMB1 session is refused.

- [x] **Step 2: Run tests to verify they fail**

Run both listed tests. Expected: no fallback/active routing exists.

- [x] **Step 3: Implement fallback and active-path selection**

On Automatic+PROT, close any partial SMB2 state and call the existing SMB1 connector. Set active_version only after success; preserve existing state/backoff. Route active SMB1 shares through the existing SMB1 folder/path logic without changing explicit configurations.

- [x] **Step 4: Run tests to verify they pass**

Run both listed tests plus `test_smb1_device_namespace.py`; all PASS.

### Task 4: Live status, docs, and verification

**Files:**
- Modify: `src/dash_devices.c`, `docs-site/content/smb.html`, `docs-site/content/devices.html`, `README.md`, `CHANGELOG.md`
- Test: `tests/test_smb_connect_state.py`, `tests/test_device_input.py` or a focused existing device-list harness
- Modify: `.github/workflows/compile.yml`

**Interfaces:**
- Consumes: connected shares' active_version from Task 3.

- [x] **Step 1: Write failing status tests**

Assert all four exact labels from no/one/both active protocol families, including an Automatic share that fell back.

- [x] **Step 2: Run tests to verify they fail**

Run the selected device/status tests. Expected: the current count-only label remains.

- [x] **Step 3: Implement and document the labels**

Replace the Network row's connected count with the exact four labels; retain existing absent/error text. Document Automatic, exact VERSION enforcement, and live status.

- [x] **Step 4: Verify all deliverables**

Run focused tests, the full host workflow, standard and DS34 packed LLVM builds, variant/toolchain checks, and docs build. Exercise status in PCSX2; do not claim live server negotiation without a live test.

- [x] **Step 5: Commit**

```bash
git add include src iop/smb2man tests docs docs-site README.md CHANGELOG.md .github/workflows/compile.yml docs/superpowers
git commit -m "Add SMB automatic negotiation and protocol status"
```
