wfview Android (arm64-v8a, network-client-only) build environment notes
=========================================================================
Generated 2026-07-09. Everything below was verified hands-on on this machine
(Windows 11, C:\Claude checkout), not guessed. Paths are absolute and
copy-pasteable. This file documents environment/toolchain state only — it
does not modify C:\Claude\wfview\wfview.pro (another agent owns that file).


1. Android NDK
---------------
Installed (side-by-side SDK layout) at:

    C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724

This is NDK r27d. The version string is NOT the commonly-guessed
27.2.12479018 — confirmed directly from
C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724\source.properties:

    Pkg.Desc = Android NDK
    Pkg.Revision = 27.3.13750724
    Pkg.BaseRevision = 27.3.13750724
    Pkg.ReleaseName = r27d

The original zip (C:\Android\ndk-download\android-ndk-r27d-windows.zip,
781MB, 7935 files) had been partially/incompletely extracted to
Sdk\ndk_extract_tmp by a previous attempt (7935 vs zip's actual 7935 file
entries — it turned out that count was coincidentally right, but the
extraction was still incomplete/unverified, so it was deleted and redone
cleanly). It was re-extracted using Windows' built-in bsdtar
(C:\WINDOWS\system32\tar.exe, supports zip natively, ~26 seconds for the
whole archive) to C:\Android\ndk-extract-tmp\android-ndk-r27d, then moved
into place as Sdk\ndk\27.3.13750724 (no double-nesting — ndk-build.cmd,
toolchains\, build\, etc. are directly under 27.3.13750724\). All temp
extraction directories (Sdk\ndk_extract_tmp, C:\Android\ndk-extract-tmp)
were removed afterward.

Toolchain verified working:

    C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android24-clang.cmd

Ran `--version` (reports "Android clang version 18.0.4 ... Target:
aarch64-unknown-linux-android24") and did a real compile of a trivial .c
file to a .o; llvm-objdump confirmed `file format elf64-littleaarch64`,
architecture aarch64. This bin directory has clang wrappers for API levels
21 through 29 (aarch64-linux-android21-clang.cmd ... 29-clang.cmd).

API level chosen: 24 (android-24). Reasoning: task asked for >=24 (a
reasonable modern minSdk floor) and <=35 (must be present among installed
SDK platforms). Installed SDK platforms on this machine are android-35 and
android-36.1, so 24 comfortably satisfies both bounds and is what the NDK
bin dir actually offers (21-29 range). Opus was built against android-24
(see section 3) — since that's a floor, it's compatible with any higher
minSdk/ANDROID_PLATFORM the app itself ends up using (Qt6.8's android-clang
mkspec defaults to android-28 if ANDROID_NDK_PLATFORM is not set — see
section 5 — 24 <= 28, so no conflict).

The NDK also bundles a native Windows GNU Make (NOT MinGW/MSYS, but not
cmd-only either — see section 4 build notes on shell behavior):

    C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724\prebuilt\windows-x86_64\bin\make.exe
    (GNU Make 4.3, "Built for Windows32")

This was used as CMAKE_MAKE_PROGRAM for the opus build (section 3) and is
also relevant for building wfview itself — see section 5.


2. CMake
--------
Neither the Android SDK (no Sdk\cmake\ dir, no Sdk\cmdline-tools, no
sdkmanager anywhere on this machine) nor the system PATH had cmake.
winget confirmed Kitware.CMake 4.3.4 is installable but that goes through
an MSI installer + PATH/registry refresh that a fresh shell wouldn't pick
up immediately, so instead (as instructed, "last resort") the official
portable Kitware CMake 3.31.5 Windows x86_64 ZIP was downloaded directly
from GitHub releases and extracted (no installer) to:

    C:\Android\cmake-3.31.5-windows-x86_64\bin\cmake.exe

Verified: `cmake --version` -> "cmake version 3.31.5". This has no bundled
Ninja, so the NDK's own make.exe (section 1) was used as the CMake
generator's make program with the "MinGW Makefiles" generator (this
generator produces recipes runnable by a native Windows GNU Make without
needing sh.exe — confirmed this combination builds cleanly).

The download/staging directory C:\Android\cmake-download was removed after
extraction; only C:\Android\cmake-3.31.5-windows-x86_64 remains.


3. libopus (Opus audio codec) — cross-compiled for Android arm64-v8a
----------------------------------------------------------------------
Source: official GitHub mirror https://github.com/xiph/opus.git, tag
v1.5.2 (shallow clone --depth 1 --branch v1.5.2; `git describe --tags`
confirms v1.5.2 after checkout).

Build type: static library (BUILD_SHARED_LIBS=OFF), Release, using opus's
native CMake build (opus's own CMakeLists.txt, min CMake 3.16 — our
3.31.5 satisfies this) with the NDK's CMake Android toolchain file.

Exact configure command used (all paths Windows-native forward-slash form
as required by CMake on Windows):

    C:/Android/cmake-3.31.5-windows-x86_64/bin/cmake.exe -G "MinGW Makefiles" ^
      -DCMAKE_MAKE_PROGRAM="C:/Users/shinjo/AppData/Local/Android/Sdk/ndk/27.3.13750724/prebuilt/windows-x86_64/bin/make.exe" ^
      -DCMAKE_TOOLCHAIN_FILE="C:/Users/shinjo/AppData/Local/Android/Sdk/ndk/27.3.13750724/build/cmake/android.toolchain.cmake" ^
      -DANDROID_ABI=arm64-v8a ^
      -DANDROID_PLATFORM=android-24 ^
      -DBUILD_SHARED_LIBS=OFF ^
      -DOPUS_BUILD_TESTING=OFF ^
      -DOPUS_BUILD_PROGRAMS=OFF ^
      -DCMAKE_BUILD_TYPE=Release ^
      <path-to-opus-source>

Then built with:

    C:/Android/cmake-3.31.5-windows-x86_64/bin/cmake.exe --build . --config Release -- -j 4

Configure succeeded (only harmless CMake<3.10-compatibility deprecation
warnings from the NDK's own toolchain files — not an error). Build
succeeded fully, including ARM NEON intrinsics files
(celt_neon_intr.c, pitch_neon_intr.c, biquad_alt_neon_intr.c,
LPC_inv_pred_gain_neon_intr.c, NSQ_del_dec_neon_intr.c) with
OPUS_MAY_HAVE_NEON runtime detection enabled. Output: libopus.a
(3,400,452 bytes).

Architecture verified with the NDK's own llvm-readobj:

    C:\...\ndk\27.3.13750724\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readobj.exe --file-headers libopus.a

    -> Format: elf64-littleaarch64, Arch: aarch64, Machine: EM_AARCH64 (0xB7)

The scratch clone/build tree (opus source + build dir) was deleted after
the artifacts were copied out — it is NOT preserved on disk. If you need
to rebuild opus (e.g. different API level, debug build, or an updated
opus version), redo the clone/configure/build steps above from scratch;
they took under 2 minutes total on this machine.


4. Opus install layout (what wfview.pro already expects)
------------------------------------------------------------
Installed at, exactly matching what C:\Claude\wfview\wfview.pro already
references (verified by grep — do not need to edit wfview.pro, it's
already wired up):

    wfview.pro line 293: android:LIBS += -L../opus/android/arm64-v8a -lopus
    wfview.pro line 310: !linux|android:INCLUDEPATH += ../opus/include
    audioconverter.h / tciserver.h: #include "opus/opus.h"

Concretely on disk:

    C:\Claude\opus\include\opus\opus.h
    C:\Claude\opus\include\opus\opus_custom.h
    C:\Claude\opus\include\opus\opus_defines.h
    C:\Claude\opus\include\opus\opus_multistream.h
    C:\Claude\opus\include\opus\opus_projection.h
    C:\Claude\opus\include\opus\opus_types.h
    C:\Claude\opus\android\arm64-v8a\libopus.a   (3,400,452 bytes, aarch64 static lib, see section 3)

Since C:\Claude\opus sits as a sibling of C:\Claude\wfview, the relative
paths "../opus/android/arm64-v8a" and "../opus/include" in wfview.pro
resolve correctly for any qmake shadow-build directory that is itself a
sibling of both (e.g. C:\Claude\wfview-build-android-arm64\ — see section
5's qmake invocation, which assumes exactly this layout).


5. Building wfview itself for Android arm64-v8a
--------------------------------------------------
Qt 6.8.1 Android kit confirmed present:

    qmake:      C:\Qt\6.8.1\android_arm64_v8a\bin\qmake.bat
    mkspec:     android-clang (C:\Qt\6.8.1\android_arm64_v8a\mkspecs\android-clang\qmake.conf)
    host tools: C:\Qt\6.8.1\mingw_64\bin\  (qmake6.exe, androiddeployqt.exe, androiddeployqt6.exe)

Environment variables required (read directly out of
C:\Qt\6.8.1\android_arm64_v8a\mkspecs\android-clang\qmake.conf and
mkspecs\qdevice.pri — the mkspec's *defaults* point at nonexistent Linux
paths like /opt/android/sdk and /opt/android/android-ndk-r26b, so these
MUST be overridden via env vars or qmake will error out):

    ANDROID_SDK_ROOT      = C:\Users\shinjo\AppData\Local\Android\Sdk
    ANDROID_NDK_ROOT       = C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724
    ANDROID_NDK_HOST        = windows-x86_64   (matches the mkspec's own default, but set explicitly for safety)
    ANDROID_NDK_PLATFORM  = android-24         (optional; mkspec's own DEFAULT_ANDROID_PLATFORM is android-28
                                                  if unset. 24 aligns with the opus build in section 3 and is
                                                  a lower/compatible floor either way. Set it if you want the
                                                  app's own minSdk/ANDROID_MIN_SDK_VERSION to be 24 instead of 28.)
    JAVA_HOME             = C:\Program Files\Android\Android Studio\jbr
                                                  (OpenJDK 21 JBR bundled with Android Studio; confirmed via
                                                  `java -version` -> openjdk 21.0.10. There is no other java
                                                  on this machine. Needed by androiddeployqt/Gradle for
                                                  javac/keytool/jarsigner, not by the qmake configure step
                                                  itself, but set it before running qmake anyway.)

ANDROID_SDK_BUILD_TOOLS_REVISION is auto-picked as the highest version
under Sdk\build-tools (currently 37.0.0, from installed 34.0.0/36.1.0/37.0.0)
if not set explicitly — no action needed unless you want to pin it.

PowerShell copy-paste block to set the environment for a build session:

    $env:ANDROID_SDK_ROOT = "C:\Users\shinjo\AppData\Local\Android\Sdk"
    $env:ANDROID_NDK_ROOT = "C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724"
    $env:ANDROID_NDK_HOST = "windows-x86_64"
    $env:ANDROID_NDK_PLATFORM = "android-24"
    $env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
    $env:PATH = "$env:JAVA_HOME\bin;C:\Program Files\Git\usr\bin;" + $env:PATH

**IMPORTANT, confirmed the hard way in a later session (see section 6):
prefer Bash/Git-Bash `export VAR="C:/like/this"` (forward slashes) over the
PowerShell `$env:` block above (backslashes) whenever you're about to
re-run qmake.** qmake bakes ANDROID_NDK_ROOT verbatim into the generated
Makefile's compiler paths; if it contains backslashes, GNU Make's
`/usr/bin/sh` swallows them as escape characters and the compiler path
comes out mangled (e.g. `C:UsersshinjoAppData...` with the separators
gone), failing the build with exit 127. This only bites you when qmake is
actually re-run (e.g. after editing wfview.pro or adding/removing a .qrc,
RESOURCES entry, etc.) — plain incremental `make` reusing an
already-correct Makefile is unaffected either way.

Exact qmake invocation (VERIFIED — actually ran successfully on this
machine end-to-end; produced a valid Makefile + apk target). Use an
out-of-source shadow build directory that is a sibling of both wfview and
opus (so wfview.pro's "../opus/..." relative paths resolve):

    New-Item -ItemType Directory -Force -Path "C:\Claude\wfview-build-android-arm64"
    Set-Location "C:\Claude\wfview-build-android-arm64"
    & "C:\Qt\6.8.1\android_arm64_v8a\bin\qmake.bat" "C:\Claude\wfview\wfview.pro" -spec android-clang

This generates Makefile, android-<target>-deployment-settings.json, and
object_script.lib<target>_arm64-v8a.so in the shadow build dir. Confirmed
targets present in the generated Makefile include a working `apk` target
that invokes:

    'C:\Qt\6.8.1\mingw_64\bin\androiddeployqt.exe' --input <shadow-dir>/android-wfview-deployment-settings.json --output <shadow-dir>/android-build --apk <shadow-dir>/android-build/wfview.apk

Note (section 6 adds detail): this qmake invocation reliably prints 3
repeated garbled/mojibake "path not found"-style lines to stderr even on a
fully successful run — confirmed harmless (Makefile/deployment
json/object_script all generated correctly, exit code 0, and a subsequent
full build succeeds). Not root-caused to an exact single $$system() call,
but does not block anything.

IMPORTANT — shell/make caveat (verified by direct testing): qmake's
android-clang mkspec sets MAKEFILE_GENERATOR = UNIX, so the generated
Makefile's recipes use POSIX commands directly (DEL_FILE = rm -f,
CHK_DIR_EXISTS = test -d, MKDIR = mkdir -p — confirmed by grepping the
generated Makefile). Plain cmd.exe has none of these. The NDK's bundled
make.exe (section 1) DOES work correctly for this — but only if rm/mkdir/
test/sh are reachable on PATH when it runs, which they are inside Git Bash
(confirmed by a live test: a Makefile with `rm -f`/`mkdir -p`/`test -d`
recipes ran cleanly end-to-end via
C:\...\ndk\27.3.13750724\prebuilt\windows-x86_64\bin\make.exe when
invoked from Git Bash). Git for Windows' POSIX tools live at
C:\Program Files\Git\usr\bin (rm.exe, sh.exe present there) and
C:\Program Files\Git\bin\bash.exe. Two ways to build:

  (a) Run the actual `make`/`make apk` step from Git Bash (simplest —
      Git Bash already has rm/mkdir/test/sh on PATH). Still export the
      same ANDROID_*/JAVA_HOME env vars first (bash `export VAR=...`
      syntax, forward slashes or /c/... form both fine).
  (b) Or, from PowerShell/cmd, prepend C:\Program Files\Git\usr\bin to
      PATH (as shown in the PowerShell block above) before invoking
      make.exe, so rm/mkdir/test/sh resolve.

Build commands (from the shadow dir, after qmake above has run):

    & "C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724\prebuilt\windows-x86_64\bin\make.exe"
    & "C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724\prebuilt\windows-x86_64\bin\make.exe" apk

The first builds the native libwfview_arm64-v8a.so; `make apk` packages it
via androiddeployqt.exe into android-build\wfview.apk.

**Section 6 supersedes this for the actual verified day-to-day workflow —
`make apk` alone was found to sometimes use a stale .so (see section 6);
always do the explicit install+hash-verify step before packaging.**


6. Day-to-day rebuild workflow (verified working repeatedly across many
   rebuild cycles in a later session)
--------------------------------------------------------------------------
Env vars — Bash, forward slashes (see the section 5 warning above):

    export ANDROID_SDK_ROOT="C:/Users/shinjo/AppData/Local/Android/Sdk"
    export ANDROID_NDK_ROOT="C:/Users/shinjo/AppData/Local/Android/Sdk/ndk/27.3.13750724"
    export ANDROID_NDK_HOST="windows-x86_64"
    export ANDROID_NDK_PLATFORM="android-24"
    export JAVA_HOME="C:/Program Files/Android/Android Studio/jbr"
    export PATH="/c/Users/shinjo/AppData/Local/Android/Sdk/ndk/27.3.13750724/prebuilt/windows-x86_64/bin:$PATH"

Rebuild only needed when .cpp/.h files changed (no .pro/.qrc changes):

    cd /c/Claude/wfview-build-android-arm64
    make.exe -j4

Re-run qmake first ONLY if wfview.pro changed or a .qrc's file list changed
(e.g. adding a new resource file to RESOURCES) — see section 5 for the
qmake invocation, but run it from Bash with the forward-slash env vars
above, not PowerShell.

Install + verify (always do this before packaging — `make apk` alone was
observed to leave Gradle using a stale native .so due to up-to-date
caching in the sub-make invocation):

    make.exe -f Makefile INSTALL_ROOT=/C/Claude/wfview-build-android-arm64/android-build install
    sha256sum libwfview_arm64-v8a.so android-build/libs/arm64-v8a/libwfview_arm64-v8a.so
    # the two hashes MUST match before packaging

Package via Gradle (do NOT pass --android-platform android-33 — this
forces compileSdk 33, which conflicts with AndroidX's requirement of
compileSdk >=34 for androidx.core:core:1.13.1 and
androidx.annotation:annotation-experimental:1.4.0, and the build fails
with a CheckAarMetadata error. Omit the flag entirely so it auto-selects
the highest installed platform, android-36.1 on this machine, which
works):

    export PATH="/c/Users/shinjo/AppData/Local/Android/Sdk/platform-tools:$JAVA_HOME/bin:$PATH"
    "/c/Qt/6.8.1/mingw_64/bin/androiddeployqt6.exe" --input android-wfview-deployment-settings.json --output android-build --gradle

Install to device and launch:

    adb install -r android-build/build/outputs/apk/debug/android-build-debug.apk
    adb shell input keyevent KEYCODE_WAKEUP
    adb shell wm dismiss-keyguard
    adb shell am force-stop org.wfview.wfview
    adb shell monkey -p org.wfview.wfview -c android.intent.category.LAUNCHER 1

Screenshot for verification:

    adb exec-out screencap -p > screenshot.png

App log files (very useful for root-causing runtime issues — much more
detailed than logcat, which doesn't seem to carry the app's own qInfo/
qWarning/qDebug output under an easily-filterable tag):

    adb shell "run-as org.wfview.wfview find /data/data/org.wfview.wfview/cache -iname '*.log'"
    adb shell "run-as org.wfview.wfview cat /data/data/org.wfview.wfview/cache/<latest>.log"

Test device this session: tablet "I11_Power" (MediaTek), adb id
9059F240801848, physical screen 1200x2000 (portrait-native; tested mostly
rotated to landscape, screenshots come out 2000x1200), density override
285 (native 320).


Summary of paths for quick reference
-------------------------------------
NDK root:        C:\Users\shinjo\AppData\Local\Android\Sdk\ndk\27.3.13750724
NDK version:      27.3.13750724 (r27d)
Clang (API 24):    <ndk>\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android24-clang.cmd
NDK make.exe:      <ndk>\prebuilt\windows-x86_64\bin\make.exe
CMake:            C:\Android\cmake-3.31.5-windows-x86_64\bin\cmake.exe (3.31.5, portable, no installer)
Opus version:      v1.5.2 (github.com/xiph/opus)
Opus headers:      C:\Claude\opus\include\opus\*.h
Opus static lib:   C:\Claude\opus\android\arm64-v8a\libopus.a (aarch64, 3,400,452 bytes)
Qt Android qmake:  C:\Qt\6.8.1\android_arm64_v8a\bin\qmake.bat
Qt host tools:      C:\Qt\6.8.1\mingw_64\bin\ (androiddeployqt.exe / androiddeployqt6.exe)
JAVA_HOME:         C:\Program Files\Android\Android Studio\jbr
Android SDK root:  C:\Users\shinjo\AppData\Local\Android\Sdk (build-tools 34.0.0/36.1.0/37.0.0, platforms android-35/android-36.1)

Note: on a different PC, all of the above C:\Users\shinjo\... and
C:\Qt\... paths need to actually exist at those locations (same Qt 6.8.1
android_arm64_v8a kit install, same NDK version side-by-side under the SDK,
same opus static lib built per section 3) before any of this applies
as-is — this file documents *this machine's* verified state, not a
portable setup script.
