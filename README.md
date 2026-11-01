# ControlCenter18 iOS 15 port

Builds ControlCenter18 3.0-beta for **iOS 15.8.8, arm64, rootless** using
**Ubuntu Linux** on GitHub Actions (no macOS runners).

## The error in the screenshots

`dlopen(.../ControlCenter18Prefs, 0x0109): Symbol not found:
_objc_release_x19` and `_objc_claimAutoreleasedReturnValue` means the binary
was built with the iOS 16 SDK. The iOS 16 Objective-C ABI uses
`objc_release_xN` / `objc_retain_xN` / `objc_claimAutoreleasedReturnValue`,
which do not exist in the iOS 15 `libobjc.A.dylib`. The preference bundle is
the first thing loaded, so it is the first thing to fail; the main SpringBoard
dylib has the same imports.

This package fixes it at the loader level:

1. `CC18ObjCLegacy.dylib` (built from `shim/`) exports all the missing ABI
   symbols as thin `mov x0, xN; b objc_release/retain` trampolines and
   `objc_claimAutoreleasedReturnValue -> objc_retain`.
2. The affected chained-fixup imports are changed to
   `BIND_SPECIAL_DYLIB_FLAT_LOOKUP` so dyld can find those symbols in the
   bundled shim instead of looking only inside `libobjc`.
3. The same shim exports `MSHookMessageEx`, `MSHookFunction` and
   `MSHookMemory`. On ElleKit it forwards to ElleKit's MobileSubstrate API;
   it has an Objective-C runtime fallback so it does not require a separate
   CydiaSubstrate installation.

All patched images are re-signed ad-hoc with `ldid` for rootless iOS 15.

## Build

Push to `main` or run the **Build ControlCenter18 iOS 15** workflow manually.
The artifact is a `iphoneos-arm64` deb.

Local build (Linux) is the same as the workflow:

```sh
python3 tools/extract_deb.py base.deb work
clang -target arm64-apple-ios15.0 ...
python3 tools/patch_imports.py ...
python3 tools/pack_deb.py work/data control out.deb 3.0-beta-ios15.21
```

## Install

1. Uninstall the upstream `com.evgeniy558.controlcenter18` first.
2. Install the deb with Sileo/Zebra (rootless `/var/jb`).
3. Open Settings -> ControlCenter18, enable the tweak, respring.

If the tweak still does not appear, check `/var/mobile/cc18compat.log` for
`CC18Compat:` lines. That log only gets entries when ElleKit's hook API is not
available and the fallback is used.
