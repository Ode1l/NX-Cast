# Step 2: Minimal Binary Plist Codec

> Status: COMPLETED
> Created: 2026-07-19

## Goal
实现 AirPlay 握手所需、带严格边界检查的最小 binary plist 读取与写入能力。

## Prerequisites
- Step 1 completed — `make test-airplay` and the AirPlay module/test harness exist.
- Files to inspect: UxPlay/RPiPlay request shapes, captured sanitized plist fixtures, existing NX-Cast allocation/error patterns.

## Deliverables
- `source/protocol/airplay/protocol/plist.[ch]` 支持所需 dictionary、array、string、data、integer、real 和 boolean 类型。
- 固定二进制夹具以可审查的 `.bplist.hex` 保存，覆盖正常、截断、递归过深、超大长度、未知类型和 round-trip 行为。

## Plan
- [x] `rg` both reference trees for plist keys and value types — inventory only the subset used by planned endpoints.
- [x] `write` `source/protocol/airplay/protocol/plist.h` — define owned value tree, limits and explicit error codes.
- [x] `write` `source/protocol/airplay/protocol/plist.c` — implement bounded binary plist decode/encode without libplist.
- [x] `write` `scripts/fixtures/airplay/plist/` — add sanitized binary fixtures as reviewable hex and malformed variants.
- [x] `write` `scripts/test_airplay_plist.c` and `edit` `makefile` — keep lifecycle and plist test concerns separate under `make test-airplay`.
- [x] `bash` `make test-airplay` — all plist cases pass under `-Wall -Wextra -Werror`.

## Quality Checklist
- [x] Evidence-before-edit: UxPlay/RPiPlay use dict/array/string/data/uint/real/bool; impact search is currently isolated to `source/protocol/airplay`.
- [x] Existing pattern / reuse checked: no suitable plist dependency exists in `/opt/devkitpro/portlibs/switch`.
- [x] Contract understood: parser owns allocations; callers receive owned typed values or a precise error, and container insertion transfers ownership only on success.
- [x] Risk reviewed: correctness / API / security / performance / project-fit; integer overflow, recursion, allocation amplification and malformed offsets.
- [x] Mitigation recorded: 1 MiB byte limit, 32 depth, 4096 nodes, 1024 container items and malformed fixture tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] ASan/UBSan host tests and Clang static analysis exit 0.
- [x] Python `plistlib` and macOS `plutil` accept the generated round-trip plist.
- [x] Strict libmpv/deko3d Switch build exits 0.

## Test Checklist
- [x] Valid UxPlay-shaped fixtures round-trip; malformed fixtures, every single-byte mutation and every truncation fail safely or decode without leaks/crashes.

## Implementation Notes
- Added a clean-room `bplist00` value tree and codec for bool, uint, real, string, data, array and dictionary values; no libplist or runtime endpoint was introduced.
- The decoder validates the header, trailer, object/offset/reference tables, UTF-8/UTF-16 and all arithmetic before allocation or traversal. Fixed limits cap input/output at 1 MiB, depth at 32, nodes at 4096, container entries at 1024, strings at 64 KiB and data at 512 KiB.
- Container mutators transfer child ownership only on success and reject shared/directly cyclic children. Decoding also rejects active-reference cycles and duplicate dictionary keys.
- Unsigned integers preserve their raw 1/2/4/8-byte bit pattern to match the reference plist API; independent plist readers may display a high-bit 64-bit value as signed.
- Kept lifecycle and plist tests in separate host executables under `make test-airplay`; reviewable hex fixtures cover a normal AirPlay-shaped response, depth overflow, oversized data and an unsupported object marker.

## Files Changed
- `makefile`
- `source/protocol/airplay/protocol/plist.h`
- `source/protocol/airplay/protocol/plist.c`
- `scripts/test_airplay_plist.c`
- `scripts/fixtures/airplay/plist/info-response.bplist.hex`
- `scripts/fixtures/airplay/plist/depth-limit.bplist.hex`
- `scripts/fixtures/airplay/plist/oversize-data.bplist.hex`
- `scripts/fixtures/airplay/plist/unsupported-object.bplist.hex`
- `plans/2026-07-19-airplay-video-mirroring/plan.md`
- `plans/2026-07-19-airplay-video-mirroring/steps/step-2.md`
- `plans/context.md`
