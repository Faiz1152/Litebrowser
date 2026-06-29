LITE BROWSER - HOW TO BUILD IT
================================

WHAT THIS IS
-------------
A tiny Windows app: one white toolbar with just a search/address box,
and the actual web page filling the rest of the window. No bookmarks,
no profile, no sign-in, no AI panel, no branding anywhere.

It's built on top of CEF (Chromium Embedded Framework) - that's the
"Chrome DLL" you mentioned. CEF is what actually fetches and renders
web pages; everything I wrote is just the thin shell around it.

IMPORTANT - ONE THING TO CLEAR UP
-----------------------------------
A single .dll is NOT enough to run a CEF app. The official CEF package
is a whole folder containing:

  - libcef.dll          (the main engine - this is probably your "chrome.dll")
  - chrome_elf.dll
  - d3dcompiler_47.dll, libEGL.dll, libGLESv2.dll  (graphics)
  - icudtl.dat           (text/language data)
  - v8_context_snapshot.bin
  - *.pak files          (resources)
  - a "locales" folder

All of those need to sit in the SAME folder as the final .exe, or it
will fail to start. If what you have is just the one DLL by itself,
you'll need the rest of that folder from wherever you got it - they
all come bundled together from cef-builds.spotifycdn.com (the official
CEF binary distribution site), under whatever version you downloaded.

The CMakeLists.txt I wrote actually does this copying for you
automatically IF you point CEF_ROOT at the full extracted CEF folder
(see below) - it copies the "Release" and "Resources" folders next to
the exe after every build.

WHAT YOU NEED INSTALLED
-------------------------
1. Visual Studio 2022 (Community edition is free) with the
   "Desktop development with C++" workload.
2. CMake (Visual Studio's installer can add this automatically).
3. The CEF binary distribution folder - the SAME one your DLL came
   from, fully extracted, not just the DLL pulled out of it.

FOLDER STRUCTURE OF THIS PROJECT
-----------------------------------
litebrowser/
  CMakeLists.txt
  src/
    main.cpp           <- window, toolbar, address bar logic
    app.h               <- required CEF bootstrap class
    client_handler.h/.cpp <- tells the browser window how to behave
  resources/
    app.manifest        <- Windows app metadata (DPI awareness)

HOW TO BUILD
--------------
1. Open a "Developer Command Prompt for VS 2022".
2. cd into the litebrowser folder.
3. Run:

     cmake -B build -DCEF_ROOT="C:\path\to\your\cef_binary_folder"
     cmake --build build --config Release

4. Your finished app appears at:

     build\Release\LiteBrowser.exe

   sitting in the same folder as libcef.dll and all its supporting
   files, copied there automatically.

5. Double-click LiteBrowser.exe to run it.

NOTES / THINGS YOU MIGHT WANT TO TWEAK
-----------------------------------------
- It opens to Google by default. Change the URL string near the
  bottom of main.cpp (CefBrowserHost::CreateBrowser line) if you want
  a different start page, or change it to "about:blank" for a truly
  empty white start.
- No back/forward buttons right now since you said "nothing else" -
  just the address bar. If you want simple back/forward (e.g. via
  Alt+Left/Right keys instead of buttons), that's a small addition
  I can make.
- "settings.no_sandbox = true" in main.cpp turns off Chromium's
  process sandboxing to keep the build simple. It's fine for personal
  use but means less isolation between tabs/processes than a normal
  browser. Say the word if you want the sandboxed version instead -
  it requires a bit more setup (a sandbox support library from CEF).
- This currently only opens ONE window/tab. If you want multiple tabs
  later, that's a bigger addition (a tab strip + multiple browser
  instances) - happy to build that as a next step.
