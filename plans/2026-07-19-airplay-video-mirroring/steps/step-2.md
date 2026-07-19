# Step 2: Minimal Binary Plist Codec

> Status: PENDING
> Created: 2026-07-19

## Goal
实现 AirPlay 握手所需、带严格边界检查的最小 binary plist 读取与写入能力。

## Prerequisites
- Step 1 completed — `make test-airplay` and the AirPlay module/test harness exist.
- Files to inspect: UxPlay/RPiPlay request shapes, captured sanitized plist fixtures, existing NX-Cast allocation/error patterns.

## Deliverables
- `source/protocol/airplay/protocol/plist.[ch]` 支持所需 dictionary、array、string、data、integer、real 和 boolean 类型。
- 固定夹具覆盖正常、截断、递归过深、超大长度、未知类型和 round-trip 行为。

## Plan
- [ ] `rg` both reference trees for plist keys and value types — inventory only the subset used by planned endpoints.
- [ ] `write` `source/protocol/airplay/protocol/plist.h` — define owned value tree, limits and explicit error codes.
- [ ] `write` `source/protocol/airplay/protocol/plist.c` — implement bounded binary plist decode/encode without libplist.
- [ ] `write` `scripts/fixtures/airplay/plist/` — add sanitized binary fixtures and malformed variants.
- [ ] `edit` `scripts/test_airplay.c` — add decode, encode, round-trip and adversarial length tests.
- [ ] `bash` `make test-airplay` — expect all plist cases to pass under `-Wall -Wextra -Werror`.

## Quality Checklist
- [ ] Evidence-before-edit: type inventory recorded from reference endpoints; impact search limited to `source/protocol/airplay`.
- [ ] Existing pattern / reuse checked: no suitable plist dependency exists in `/opt/devkitpro/portlibs/switch`.
- [ ] Contract understood: parser owns allocations; callers receive immutable typed values or a precise error.
- [ ] Risk reviewed: integer overflow, recursion, allocation amplification and malformed offsets.
- [ ] Mitigation recorded: byte/depth/node limits and malformed fixture tests.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] Strict Switch build exits 0.

## Test Checklist
- [ ] Valid UxPlay-shaped fixtures round-trip; all malformed fixtures fail without leaks or crashes.

## Implementation Notes
Pending.

## Files Changed
Pending.
