# PlayFair provenance

Source repository: https://github.com/FDH2/UxPlay

Source commit: `3ca7526387e894d6848b84c209de361c3bedd1ec`

Imported paths:

- `lib/playfair/hand_garble.c`
- `lib/playfair/modified_md5.c`
- `lib/playfair/omg_hax.c`
- `lib/playfair/omg_hax.h`
- `lib/playfair/playfair.c` (renamed to `playfair_decrypt.c`)
- `lib/playfair/playfair.h` (renamed to `playfair_decrypt.h`)
- `lib/playfair/sap_hash.c`
- `lib/playfair/LICENSE.md`
- Response tables from `lib/fairplay_playfair.c`, isolated in `replies.c`

The PlayFair algorithm is GPL-3.0. The response-table wrapper carries an
LGPL-2.1-or-later notice. NX-Cast is GPL-3.0, so the combined source remains
GPL-3.0 when distributed as one executable.

NX-Cast changes are limited to file/header renaming, trailing-whitespace
normalization, strict-compiler warning cleanup, two defined-unsigned-arithmetic
fixes, bounds-safe response access, and the adapter in
`source/protocol/airplay/security/fairplay.c`. These changes do not claim an
independent derivation of the compatibility algorithm or Apple authorization.

The NX-Cast adapter validates both protocol mode bytes before entering the
legacy algorithm. This is required because the imported table code assumes a
mode in the range 0..3 and does not perform its own bounds check.

Original imported-file SHA-256 values before NX-Cast modifications:

```text
cac6be5bb721e07d36d44577d112de63b40b8a12dd663495cd7cd600ab8743e4  hand_garble.c
850c815fd99c9f8459e2ff323c25963e20873328b838cda33fff88400fba2667  modified_md5.c
1bfdce59d4b462cf3b5c39f3d8b3f0f6a9c20eccf003128f972bcc7572f50823  omg_hax.c
839517a07a208435bce0f789f3c7a3b10d698f65509fdb6a2e601249ac4e3e88  omg_hax.h
322077cfb1f4ca555c302ec4f4f718c7daa42fbb358f9c7738e95d522263b707  playfair.c
5160ba53c5557f207d81998cc17a126ecb07842dc157c002ed54b397c536e613  playfair.h
ec90021e184776e825582486588d3edf61f70b494bf74915e28bd6d37870a70d  sap_hash.c
0b7cf235e6136b426f00870c3200bac308950973d062a733bdc3fc1eca36fa06  LICENSE.md
```
