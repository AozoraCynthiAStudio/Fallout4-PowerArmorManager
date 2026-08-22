# Native plugin build

This directory contains the current `PowerArmorMgr` F4SE/CommonLibF4 native source.

Prerequisites:

- Visual Studio C++ build tools with an x64 developer command prompt
- xmake 3.x
- The CommonLibF4 checkout used by the project, including its `commonlibf4-frakkin64` xmake entry point

Set the dependency path before building:

```powershell
$env:COMMONLIBF4_XMAKE_ROOT = 'D:\path\to\commonlibf4-frakkin64'
```

Then run `build_dll.bat`. The script is intentionally build-only; deployment and packaging stay outside the source build.
