wfview Android (arm64-v8a, network-client-only) build notes — macOS host
=========================================================================
Companion to ANDROID_BUILD_NOTES.md, which documents the Windows machine
this port was originally built on. This file is written for continuing the
same work from a Mac. Unlike the Windows notes, the exact paths below have
**not** been verified hands-on on an actual Mac — they're the standard/
documented install locations for Qt and Android Studio on macOS. Verify each
one on the actual machine before trusting it; if a path doesn't exist,
`find`/`mdfind` for it rather than guessing further.

The good news: building Android targets from a macOS host avoids the
single biggest class of problem hit on Windows this session — there is no
MSYS/Git-Bash-vs-PowerShell path mangling, no backslash-vs-forward-slash
env var issue, and no cmd.exe-vs-POSIX-shell Makefile mismatch, because
macOS's Terminal is already a real POSIX shell throughout (zsh by default).
qmake's android-clang mkspec generates Unix-style Makefile recipes (rm -f,
mkdir -p, etc.) which just work natively.

Get this repo first:

    git clone https://github.com/kazushinjo/wfview-android.git wfview
    cd wfview
    git checkout android-port   # the working branch this port lives on

wfview.pro needs four sibling directories next to wfview/ (i.e. inside the
same parent folder as the wfview/ clone) — see "Dependencies" below for
exactly what each one is and how to reproduce it.


1. Qt 6.8.1 with the Android arm64-v8a kit
--------------------------------------------
Install via the Qt Online/Maintenance Tool (Qt account required, same as
on Windows). In the component tree for Qt 6.8.1, select "Android" (this
installs all Android ABI kits, arm64-v8a included) in addition to whatever
desktop macOS kit you may already have.

Typical install location on macOS:

    ~/Qt/6.8.1/android_arm64_v8a/bin/qmake        (or qmake6 -- check both)
    ~/Qt/6.8.1/android_arm64_v8a/mkspecs/android-clang/qmake.conf
    ~/Qt/6.8.1/macos/bin/                          (host tool binaries: androiddeployqt, androiddeployqt6)

On Windows, host tools (androiddeployqt.exe etc.) lived under the
`mingw_64` desktop kit's bin dir, not the android kit's own bin dir --
expect the equivalent macOS desktop kit (likely named `macos` or `clang_64`
depending on Qt version) to play that same role. Run
`find ~/Qt -iname "androiddeployqt*"` to confirm exactly where it landed.


2. Android SDK + NDK r27d
---------------------------
If Android Studio is (or gets) installed, its SDK typically lands at:

    ~/Library/Android/sdk

Install NDK r27d (27.3.13750724) specifically -- either via Android
Studio's SDK Manager (SDK Tools tab -> NDK (Side by side) -> pick version
27.3.13750724) or by downloading
`android-ndk-r27d-darwin.zip` (or `-darwin-x86_64`/`-darwin-arm64`
depending on how JetBrains packages it for the current NDK release
channel -- check https://developer.android.com/ndk/downloads for the exact
current asset name for r27d) and extracting to:

    ~/Library/Android/sdk/ndk/27.3.13750724

JAVA_HOME: Android Studio bundles its own JBR (JetBrains Runtime) at:

    /Applications/Android Studio.app/Contents/jbr/Contents/Home

Confirm with `"$JAVA_HOME/bin/java" -version` once set.

adb lives at `~/Library/Android/sdk/platform-tools/adb` once platform-tools
is installed via SDK Manager (or `brew install android-platform-tools`).


3. Dependencies (the four sibling directories wfview.pro expects)
----------------------------------------------------------------------
These must sit as siblings of the wfview/ clone (e.g. if wfview is at
~/dev/wfview, these go at ~/dev/opus, ~/dev/eigen, etc.) — wfview.pro
references them via relative paths like `../opus`, `../eigen`.

**eigen** — pinned to tag 3.4.0 (commit 3147391d946bb4b6c68edd901f2add6ac1f31f8c):

    git clone --branch 3.4.0 --depth 1 https://gitlab.com/libeigen/eigen.git

**r8brain-free-src** — pinned to commit e71c31bf320f84210bb4bdcb57e296c39ce940f9
(no tag; this was just the tip of the default branch as of the clone date,
2026-05-25). Clone and check out that exact commit for parity:

    git clone https://github.com/avaneev/r8brain-free-src.git
    cd r8brain-free-src && git checkout e71c31bf320f84210bb4bdcb57e296c39ce940f9

**qcustomplot** — version 2.1.1 (`QCUSTOMPLOT_VERSION_STR` in
qcustomplot.h). Only `qcustomplot.cpp`/`qcustomplot.h` are needed (wfview.pro
compiles them directly as sources for the android target — see
wfview.pro around line 576-580 — no separate shared-lib build required,
unlike the desktop macOS/Linux paths in the same .pro file which DO expect
a prebuilt libqcustomplot). Download the 2.1.1 source archive from the
official site, https://www.qcustomplot.com/index.php/download (look for
the "Source" or "Full package" 2.1.1 release), and place `qcustomplot.cpp`
+ `qcustomplot.h` directly at the top of the `qcustomplot/` sibling
directory.

**opus** — v1.5.2, built as a static library for Android arm64-v8a. This is
a real cross-compiled binary artifact, not just source — rebuild it fresh
on the Mac rather than trying to transplant the Windows-built one (ABI
compatibility between a Windows-hosted vs Mac-hosted NDK clang build of the
same target triple should be fine in principle, but rebuilding takes under
2 minutes and removes any doubt). Needed layout afterward (matches what
wfview.pro already expects — see wfview.pro lines ~293 and ~310):

    opus/include/opus/*.h
    opus/android/arm64-v8a/libopus.a

Build steps (macOS has cmake and make natively via Xcode Command Line
Tools -- `xcode-select --install` if not already present; `brew install
cmake` also works and avoids relying on Xcode's older bundled cmake if
present):

    git clone --depth 1 --branch v1.5.2 https://github.com/xiph/opus.git opus-src
    cd opus-src
    cmake -G "Unix Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-24 \
      -DBUILD_SHARED_LIBS=OFF \
      -DOPUS_BUILD_TESTING=OFF \
      -DOPUS_BUILD_PROGRAMS=OFF \
      -DCMAKE_BUILD_TYPE=Release \
      .
    cmake --build . --config Release -- -j$(sysctl -n hw.ncpu)

Then copy the resulting `libopus.a` to `opus/android/arm64-v8a/libopus.a`,
and opus's `include/` headers to `opus/include/` (matching the Windows
layout documented in ANDROID_BUILD_NOTES.md section 4), as siblings of
the wfview/ clone.


4. Environment variables (zsh/bash Terminal)
------------------------------------------------
    export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
    export ANDROID_NDK_ROOT="$HOME/Library/Android/sdk/ndk/27.3.13750724"
    export ANDROID_NDK_HOST="darwin-x86_64"   # check: may be "darwin-arm64" if Qt/NDK ships a native Apple Silicon host toolchain and you're on M-series hardware -- confirm with `ls "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/"`
    export ANDROID_NDK_PLATFORM="android-24"
    export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
    export PATH="$ANDROID_SDK_ROOT/platform-tools:$JAVA_HOME/bin:$PATH"

No forward/backslash concerns here (this is the whole Windows-specific
problem class from ANDROID_BUILD_NOTES.md section 5/6 that doesn't apply
on macOS) — but ANDROID_NDK_HOST must still exactly match how the NDK's
own toolchain directory is actually named on disk (check with `ls`,
don't assume).


5. Build workflow (same shape as the Windows section 6 workflow, adjusted
   for macOS paths and a plain shell)
--------------------------------------------------------------------------
Use an out-of-source shadow build directory that is a sibling of wfview/
and the four dependency dirs above:

    mkdir -p ~/dev/wfview-build-android-arm64
    cd ~/dev/wfview-build-android-arm64
    ~/Qt/6.8.1/android_arm64_v8a/bin/qmake "$HOME/dev/wfview/wfview.pro" -spec android-clang

    make -j$(sysctl -n hw.ncpu)

    make -f Makefile INSTALL_ROOT="$PWD/android-build" install
    shasum -a 256 libwfview_arm64-v8a.so android-build/libs/arm64-v8a/libwfview_arm64-v8a.so
    # the two hashes MUST match before packaging

    ~/Qt/6.8.1/macos/bin/androiddeployqt6 --input android-wfview-deployment-settings.json --output android-build --gradle
    # (do NOT pass --android-platform -- let it auto-pick the highest installed SDK platform,
    # same reasoning as the Windows notes: forcing an old one conflicts with AndroidX's
    # compileSdk >=34 requirement and the build fails)

    adb install -r android-build/build/outputs/apk/debug/android-build-debug.apk
    adb shell input keyevent KEYCODE_WAKEUP
    adb shell wm dismiss-keyguard
    adb shell am force-stop org.wfview.wfview
    adb shell monkey -p org.wfview.wfview -c android.intent.category.LAUNCHER 1

Screenshot / log retrieval (same idea as Windows section 6):

    adb exec-out screencap -p > screenshot.png
    adb shell "run-as org.wfview.wfview find /data/data/org.wfview.wfview/cache -iname '*.log'"
    adb shell "run-as org.wfview.wfview cat /data/data/org.wfview.wfview/cache/<latest>.log"


6. Known open items carried over from the Windows session (2026-07-09)
---------------------------------------------------------------------------
- Settings/band-select/frequency-entry popups were just given the same
  fit-to-screen treatment as the main window (commit "Fit settings/band/
  frequency popups to screen too") but this was **not yet build-tested on
  any device** before the switch to Mac -- verify this first.
- RX latency slider (受信遅延時間) on the Settings screen does not respond
  to real finger touch/drag at all, though a synthetic `adb shell input
  swipe` was confirmed (mid-drag, not just before/after) to update both the
  value and the handle position correctly. Root cause not found — see
  [[feedback-no-speculative-fixes]]-style caution: don't guess at a fix
  without new device-level evidence. Worth checking whether the RF/AF/SQL/
  Mic/TX/Mon sliders on the main screen have the same symptom or not, and
  whether the qdarkstyle QSlider handle hit-region (qdarkstyle/style.qss,
  `QSlider::handle` rules) is unusually small.
- Audio stuttering ("ぶぶぶ" / choppy audio) reported, said to be unrelated
  to the RX latency value itself. No prior fix found in git history or
  memory for this specific symptom — needs fresh investigation, likely
  starting from the Qt Audio (QAudioSink) buffer size configuration for
  the RX path on Android, and/or the UDP audio jitter buffer sizing.
- User separately mentioned a reference repo, `kazushinjo/wfview-mac`, for
  "all screens" — interrupted before real investigation of what specifically
  to port from it. Worth reading its actual diff against upstream wfview to
  see if it independently solved the fixed-pixel-layout problem differently.
