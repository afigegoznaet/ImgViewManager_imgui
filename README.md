# imgui_ivm

Dear ImGui folder browser with a resizable folder tree and contents view. Uses SDL3 for
windowing/input and OpenGL 3 for rendering.

Prerequisites: C++17 compiler, CMake 3.24+, SDL3 3.2+ development package,
OpenGL, Fontconfig, FreeType, ICU, Qt6 Core/Gui, libpng, and libjpeg-turbo development libraries, and an installed Dear ImGui package with its SDL3
and OpenGL3 backends and CMake configuration. On Arch Linux, install the AUR
package `imgui-full`. CMake uses the installed package and downloads no dependencies.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/imgui_ivm
```

## Install

Build a release executable and install it system-wide:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel
sudo cmake --install build/release
```

The default prefix is `/usr/local`. On Linux this installs `bin/imgui_ivm`,
`share/applications/imgui_ivm.desktop`, and `share/icons/hicolor/16x16/apps/imgui_ivm.png`.
The launcher appears as **Image View Manager (ImGui)** and uses the Qt project's icon.
It has separate executable, launcher, and icon names so both versions can coexist.
Runtime dependencies listed above must be installed on the target system.

For installation into your user account, use:

```sh
cmake --install build/release --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is in your `PATH`. To configure a different prefix at
build time, pass `-DCMAKE_INSTALL_PREFIX=/your/prefix` to CMake.

Starts in the working directory, or pass a folder: `./build/imgui_ivm /path/to/folder`.
Click a tree folder to select it, or enter a path in the address bar. Up and Refresh support navigation.
Directory scans run in a background worker; collapsed tree branches are not scanned.
Images appear in a scrollable thumbnail grid with filenames; hover for the relative path.
View → Sort by full path (the default) or Sort by filename uses locale-aware numeric
ordering, like the Qt app: image2 precedes image10. The sorting choice is saved between runs.
View → Thumbnail size adjusts the grid. All thumbnails preload first, with a 1 GiB texture cache and no entry-count limit.
Progress shows Thumbnails first, then Previews; visible thumbnails take priority.
Decoding and resizing run in a background worker; GPU uploads run on the UI thread.
A bounded texture cache is cleared when switching folders or refreshing.
JPEG decoding uses libjpeg-turbo scaling; PNG decoding uses libpng.

Omit `-G Ninja` to use CMake's default build tool. For an ImGui installation outside
the standard system prefixes, set `-DCMAKE_PREFIX_PATH=/path/to/prefix`.

The files view scans PNG/JPEG files recursively by default. Use View → Include subfolders to change the scan scope.
The file view always excludes non-image files. Both folder views hide directories
without PNG/JPEG images anywhere in their subtree. Checks run in the folder
worker and are cached until Refresh; folders appear once an image is found.
Child checks run in a fair queue, yielding after at most 128 entries or 5 ms while
retaining their traversal progress. New expansion requests take priority, and a
large subtree cannot hold up checks for its siblings. Filesystem changes invalidate
only the affected cached subtree and its ancestors. Plain filter text matches a
filename substring; `*` and `?` patterns match the complete filename. Matching
is ASCII case insensitive. Filtering uses scanned results and does not rescan.
Scans publish incremental results; changing folders cancels the previous scan.
Directory symlinks are not followed, and unreadable folders are reported.

Run scanner/filter checks with `ctest --test-dir build --output-on-failure`.

Hover over a thumbnail to show a larger image over the folder tree, like the Qt
app. View → Show preview toggles the overlay. Preview decoding has a separate
worker, scales images to the overlay size (up to 2048 pixels on the longest side), and never enlarges
a small source image. The overlay hides on scrolling, leaving the thumbnail,
filtering the image out, or changing folders. The overlay always displays only the currently hovered image.

The session is saved automatically and on exit in SDL's per-user preference
directory (`state.conf`, alongside `imgui.ini`). The last folder, window geometry,
splitter width, thumbnail size, scan/preview options,
filter, selected image, and scroll positions are restored on startup. Passing a
folder argument overrides the saved folder. If the saved folder was deleted,
startup uses the nearest existing parent. Only the active folder and its ancestors
are expanded at startup.

Live filesystem updates use inotify on Linux (background polling elsewhere).
The viewed folder and its recursive scan scope are watched, together with
expanded tree folders and their immediate children on Linux. Deleted images
are removed from the grid immediately; additions and edits trigger a background
rescan while the current results and unchanged thumbnails remain visible.
Folder navigation uses the tree; there is no separate subfolder strip.

View → Preload previews enables background scaling of every image in the current
filtered folder view (on by default and saved between runs). The hovered image
has priority. The preview texture cache can use up to 10 GiB; older textures are
evicted when it fills. Progress tracks the whole folder and stays complete after
scrolling; an evicted image is loaded again when viewed. Switching folders,
refreshing, or changing preview resolution restarts scaling.

## Capturing a crash

Run `./tools/run-instrumented.sh` (optionally followed by a folder path).
The launcher builds a separate Debug executable in `build/instrumented` with
AddressSanitizer and UndefinedBehaviorSanitizer, then runs it under GDB.
Use the app normally, reproduce the folder changes, and close the app normally.
On a crash GDB records all thread stacks; sanitizer diagnostics and application
output are saved together in `build/instrumented/crash-logs`.
The launcher prints the exact log path. GDB is required.
Leak checking is disabled because it cannot run under GDB; memory-access and
undefined-behavior checking remain enabled. This build uses your usual saved session.

Keyboard controls follow the Qt view conventions. The folder tree receives initial
focus; Tab/Shift+Tab switch between tree and grid. Both support arrows, Home/End,
Page Up/Down, and typing a filename prefix (repeating a character cycles matches).
Left collapses a tree branch or selects its parent; Right expands or enters it;
Enter toggles the branch. In the grid, Shift extends selection, Ctrl+arrows move
without selecting, Ctrl+Space toggles the current image, and Ctrl+A selects all
filtered images. Ctrl/Shift clicks use the same selection rules. Enter or a double
click opens selected images in the desktop application; Ctrl+O also opens them.
Ctrl+S opens matching PSD/EPS/AI/SVG/ORF source files when present.
Ctrl++/Ctrl+- and Ctrl+wheel change thumbnail size; Ctrl+/ resets it.
F5 refreshes, Ctrl+F focuses the filter, Ctrl+L focuses the address, and Alt+X exits.
Navigation shortcuts leave active text inputs to handle editing keys. Ctrl+I opens the export destination picker.

Right-click a thumbnail for the Qt-style image menu: Open image, Export image,
Open source file, and file information. Existing multi-selection is preserved;
right-clicking empty grid space opens the menu for the selection. Export uses
selected images and includes matching PSD/EPS/AI/SVG/ORF files. It moves files
on the same filesystem and copies them across filesystems, matching the Qt app.
Existing destination files are never overwritten; failures are listed in the
progress dialog and their originals remain. Metadata and exports run in the
background. Successfully exported images leave the current filtered view.

Grid thumbnails are restored from and saved to `.kthumbnails` in each image folder,
using the Qt app’s QDataStream format. Missing entries are generated in the worker;
updates are saved atomically when its queue is idle. Unwritable folders still work
with an in-memory cache. Matching uses filenames without timestamp checks. Large
previews are not saved to disk.
