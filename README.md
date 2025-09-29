Namida build notes (now with 57% more whimsy)
-------------------------------------------

Welcome, intrepid packager! This tiny, slightly dramatic script helps you turn the glorious mess in `app/` into a single, majestic installer that whispers "I run on Windows XP" as it slides into a folder.

What you'll want beforehand:

- innoextract (optional) — the script will try to be polite and extract an existing Inno Setup installer if you gift it one. The latest git-built innoextract tends to be the most cooperative.
- a MinGW cross-compiler on PATH (e.g. `i686-w64-mingw32-gcc`) if you want the launcher built locally.

How to summon the build gremlins:

```
bash sh.sh
```

What the script does while humming softly:

1. If you have `asm/Namida-x86_64-Installer.exe` and `innoextract`, it will attempt a polite extraction into `app/`.
2. It zips up the `app/` directory into a payload. The zip is neat and tidy (sorted and modest).
3. It tries to build the XP-compatible launcher from `bin/` using the included Makefile (MinGW toolchain required).
4. It concatenates the launcher and the zip payload, tucks a tiny footer on the end, and writes out `dist/namidiaforxp_installer.exe` which you can fling at ancient machines.

