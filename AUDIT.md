# Linux stereo presentation audit

Audited source: upstream `f54d156f5f6a5defa890d7f555ec1cb034917da1`, checked against `gkv311/sview` master on 2026-10-23.
This is a source audit, not a claim that a build or display test passed.
The scope is the image viewer and movie player on desktop Linux.

## Rendering and window ownership

Desktop Linux uses sView's own Xlib window system and OpenGL widgets.
[CMakeLists.txt:231](CMakeLists.txt#L231) enables Xlib and [CMakeLists.txt:264](CMakeLists.txt#L264) links desktop OpenGL.
[StCore/StXDisplay.cpp:49](StCore/StXDisplay.cpp#L49) opens the X display.
[StCore/StWindowImplLin.cpp:109](StCore/StWindowImplLin.cpp#L109) has an EGL conditional, but that branch still creates X windows; it is not a native Wayland backend.
The ordinary GLX path requests a stereo visual only when `attribs.IsGlStereo` is set ([StCore/StWindowImplLin.cpp:144](StCore/StWindowImplLin.cpp#L144)).
No `wl_surface`, `wayland-client`, `stereo_declare` or `_KDE_NET_WM_STEREO` implementation was found in tracked C/C++ sources or CMake text at this revision.

The normal decorated window has an X top level and a GL child covering its client area ([StCore/StWindowImplLin.cpp:238](StCore/StWindowImplLin.cpp#L238)).
The GL child at line 277 contains both media and UI.
An optional second output window is created at the root and receives no input events ([StCore/StWindowImplLin.cpp:294](StCore/StWindowImplLin.cpp#L294)).
That second window is a display-device output, not a media-only child of the UI.

Both applications register six output implementations: Anaglyph, Dual, iZ3D, Interlace, Distorted and PageFlipExt ([StMoviePlayer/StMoviePlayer.cpp:487](StMoviePlayer/StMoviePlayer.cpp#L487), [StImageViewer/StImageViewer.cpp:261](StImageViewer/StImageViewer.cpp#L261)).
`StApplication::addRenderer()` collects their device choices ([StCore/StApplication.cpp:278](StCore/StApplication.cpp#L278)).
The selected output owns the window and emits `onRedraw(view)`; `doDrawProxy()` turns this into mono when stereo output is disabled ([StCore/StApplication.cpp:258](StCore/StApplication.cpp#L258), [StCore/StApplication.cpp:298](StCore/StApplication.cpp#L298)).

## What each output writes

Here a complete view means an independently rendered eye image before display-specific filtering.
It does not mean that the original decoded samples survive without resizing.

| Output | Source and behavior | Complete independent pictures at the output boundary? |
| --- | --- | --- |
| Quad-buffer OpenGL | [StOutPageFlip/StOutPageFlip.cpp:1078](StOutPageFlip/StOutPageFlip.cpp#L1078) checks `GL_STEREO`, draws left to `GL_BACK_LEFT` and right to `GL_BACK_RIGHT`, then swaps. | Two complete rendered views, but in GL buffers, not an explicitly declared media-only SBS surface. Without stereo buffers it draws both passes into `GL_BACK` and overlays a warning. |
| Software page flipping | [StOutPageFlip/StOutPageFlip.cpp:1235](StOutPageFlip/StOutPageFlip.cpp#L1235) calls `stglDrawAggressive()` for each eye; [line 1246](StOutPageFlip/StOutPageFlip.cpp#L1246) swaps after each pass. | One complete rendered eye per presentation, not one atomic pair. Hardware-control paths may add synchronization graphics. |
| Anaglyph | [StOutAnaglyph/StOutAnaglyph.cpp:336](StOutAnaglyph/StOutAnaglyph.cpp#L336) renders two viewport-sized FBOs, then combines them with a selected shader. [Simple shader:11](StOutAnaglyph/shaders/fAnaglyphSimple.shf#L11) keeps left red and right green/blue. | No. The final picture combines and discards color components. |
| Rows, columns, checkerboard | [StOutInterlace/StOutInterlace.cpp:587](StOutInterlace/StOutInterlace.cpp#L587), [line 620](StOutInterlace/StOutInterlace.cpp#L620), [line 652](StOutInterlace/StOutInterlace.cpp#L652) discard alternating fragments. [Line 1011](StOutInterlace/StOutInterlace.cpp#L1011) adjusts parity for window placement and display settings; [line 1102](StOutInterlace/StOutInterlace.cpp#L1102) draws the eye passes with masks/filtering. | No. Each eye contributes only selected display pixels. Smoothing is an additional sampling operation. |
| SBS / top-and-bottom | [StOutDistorted/StOutDistorted.cpp:953](StOutDistorted/StOutDistorted.cpp#L953) enables stereo only in fullscreen, selects split viewports, and draws both views at [line 1057](StOutDistorted/StOutDistorted.cpp#L1057). Anamorphic modes halve viewport width or height at [line 1037](StOutDistorted/StOutDistorted.cpp#L1037). | Separate rendered views in the full split modes, but fitted to output viewports and including UI. The anamorphic modes squeeze output viewports. No KWin declaration exists. Windowed mode goes through the left/mono path. |
| Two monitors / mirror rig | [StOutDual/StOutDual.cpp:423](StOutDual/StOutDual.cpp#L423) draws left to the master window and right to the slave. Mirror modes use an FBO and reflect the right view at [line 494](StOutDual/StOutDual.cpp#L494). | Two complete rendered views on separate windows, not one shared pair. Monitor and reflection settings belong to the application today. |
| iZ3D | [StOutIZ3D/StOutIZ3D.cpp:339](StOutIZ3D/StOutIZ3D.cpp#L339) renders two eye textures, then two different shaders combine them into master and slave panel images. | No. These are device-specific panel signals, not left and right pictures. |
| Headset / distorted | [StOutDistorted/StOutDistorted.cpp:824](StOutDistorted/StOutDistorted.cpp#L824) handles OpenVR rendering; [line 1082](StOutDistorted/StOutDistorted.cpp#L1082) enlarges the HMD FBO and [line 1129](StOutDistorted/StOutDistorted.cpp#L1129) selects barrel distortion. | Device-specific eye targets or lens-distorted output. Not the edition's undistorted media surface. |

All six output modules are currently added unconditionally to the build ([CMakeLists.txt:413](CMakeLists.txt#L413)).
The movie player links them explicitly ([StMoviePlayer/CMakeLists.txt:68](StMoviePlayer/CMakeLists.txt#L68)); the image viewer has its own corresponding dependency list.
Merely hiding an output menu would leave alternate renderer registration and dependencies intact.

## Media samples and input packings

The useful existing separation is inside the texture queue, before output rendering.
[StShared/StGL/StGLTextureData.cpp:350](StShared/StGL/StGLTextureData.cpp#L350) accepts decoded left/right images and an input format.
Its reference path creates half-width SBS views at [line 369](StShared/StGL/StGLTextureData.cpp#L369), half-height TaB views at [line 401](StShared/StGL/StGLTextureData.cpp#L401), and alternating-row views at [line 434](StShared/StGL/StGLTextureData.cpp#L434).
The fallback copy path implements the same basic splits at [line 520](StShared/StGL/StGLTextureData.cpp#L520).
Separate images, frame pairs and Sisvel tiled input have paths at [line 553](StShared/StGL/StGLTextureData.cpp#L553).
These are useful starting points for preserving samples; they are not proof that the final drawable preserves them.

`StGLImageRegion` chooses a left or right texture, incorporating per-item `ToSwapLR` ([StGLWidgets/StGLImageRegion.cpp:770](StGLWidgets/StGLImageRegion.cpp#L770)).
It defaults to linear filtering ([line 231](StGLWidgets/StGLImageRegion.cpp#L231)), applies filters at [line 836](StGLWidgets/StGLImageRegion.cpp#L836), and fits the image to its widget rectangle at [line 1088](StGLWidgets/StGLImageRegion.cpp#L1088).
The anamorphic repair heuristic changes the display ratio before this fit.
Thus reusing `stglDrawView()` at the window's dimensions would resample media before KWin.
The image loader also downscales images that exceed texture limits ([StImageViewer/StImageLoader.cpp:480](StImageViewer/StImageLoader.cpp#L480), [line 572](StImageViewer/StImageLoader.cpp#L572)); large pictures need an explicit preservation strategy rather than silently retaining this behavior in the stereo copy path.

| Input family | Current source path | Qualification still required |
| --- | --- | --- |
| SBS and TaB, either order | The enum names both orders ([include/StGLStereo/StFormatEnum.h:15](include/StGLStereo/StFormatEnum.h#L15)); the queue splits and orders the views. | Full and half sources must retain stored dimensions and carry intended display geometry separately. |
| SEI and Matroska video | FFmpeg frame stereo side data is read at [StMoviePlayer/StVideo/StVideoQueue.cpp:958](StMoviePlayer/StVideo/StVideoQueue.cpp#L958); legacy metadata names are mapped at [line 233](StMoviePlayer/StVideo/StVideoQueue.cpp#L233). Explicit user selection wins at [line 1009](StMoviePlayer/StVideo/StVideoQueue.cpp#L1009). | Applicable SEI precedence, persistence/cancellation and intended aspect must be measured with the actual FFmpeg build. A frame metadata tag can overwrite the side-data result at line 997. |
| Frame sequence / separate streams | A cached frame is paired with the next at [StMoviePlayer/StVideo/StVideoQueue.cpp:1091](StMoviePlayer/StVideo/StVideoQueue.cpp#L1091); separate queues synchronize by timestamps at [line 1044](StMoviePlayer/StVideo/StVideoQueue.cpp#L1044). | Pair identity, seeks, missing first view and right-first ordering need timed fixtures. |
| MPO, JPS, ordinary JPEG | [StImageViewer/StImageLoader.cpp:297](StImageViewer/StImageLoader.cpp#L297) parses JPEG-family metadata and picks the largest matching embedded images; [line 384](StImageViewer/StImageLoader.cpp#L384) records JPS format and decodes both MPO pictures. | Eye ordering and precedence need independently marked fixtures, including an ordinary JPEG with an explicit packing choice. |
| PNS / PNG | [StShared/StAV/StAVImage.cpp:274](StShared/StAV/StAVImage.cpp#L274) uses FFmpeg's PNG decoder; [line 465](StShared/StAV/StAVImage.cpp#L465) consumes its stereo side data. Loader metadata and filename fallback appear at [StImageViewer/StImageLoader.cpp:459](StImageViewer/StImageLoader.cpp#L459). | Test `sTER` and unmarked PNS separately; availability of a decoder does not prove stereo metadata reaches the renderer. |
| Rows | A compact half-height view is created for each row parity. | The legacy tag table maps both orders to the same enum. Test both orders and 4:2:0 chroma; the copy path itself has a YUV420 concern at line 545. |
| Columns | Named in the enum and FFmpeg mapping, but [StShared/StGL/StGLTextureData.cpp:577](StShared/StGL/StGLTextureData.cpp#L577) explicitly falls through to mono copying. | This is missing input support, not a working decoder to claim as preserved. |
| Checkerboard / quincunx | The mappings are commented out at [include/StAV/stAV.h:451](include/StAV/stAV.h#L451). | A complete input API needs these cases and eye order; do not mistake checkerboard output support for input decoding. |
| Anaglyph input / panoramas | [StGLWidgets/StGLImageRegion.cpp:900](StGLWidgets/StGLImageRegion.cpp#L900) selects input color masks; [StMoviePlayer/StMoviePlayerGUI.cpp:634](StMoviePlayer/StMoviePlayerGUI.cpp#L634) exposes theater, cylinder, hemisphere, sphere and cubemap views. | Preserve these input/view capabilities. Color recovery cannot restore color information absent from the source. Projection rendering needs its own proof beyond flat sample copying. |

## UI, overlays and input

The movie player's redraw clears the buffer, selects the GUI camera eye and draws the entire GUI ([StMoviePlayer/StMoviePlayer.cpp:1964](StMoviePlayer/StMoviePlayer.cpp#L1964)).
The picture viewer does the same ([StImageViewer/StImageViewer.cpp:1250](StImageViewer/StImageViewer.cpp#L1250)).
The media region is a widget inside the root GUI, and movie subtitles are children of that media widget ([StMoviePlayer/StMoviePlayerGUI.cpp:1688](StMoviePlayer/StMoviePlayerGUI.cpp#L1688), [line 1706](StMoviePlayer/StMoviePlayerGUI.cpp#L1706)).
The picture viewer constructs the same shared media widget ([StImageViewer/StImageViewerGUI.cpp:1304](StImageViewer/StImageViewerGUI.cpp#L1304)).
`StGLImageRegion::stglDraw()` draws its children after its media pass ([StGLWidgets/StGLImageRegion.cpp:729](StGLWidgets/StGLImageRegion.cpp#L729)).
Moving that whole call to a declared surface would move subtitles and navigation graphics into it too.
Media drawing must be separated from widget-child drawing so menus, subtitles, controls and navigation stay on the ordinary 2D surface.

X button and wheel coordinates are corrected for sView's tiled output and divided by the logical rectangle ([StCore/StWindowImplLin.cpp:975](StCore/StWindowImplLin.cpp#L975)).
Pointer polling also uses the GL child's coordinates, tiled correction and that rectangle ([StCore/StWindowImpl.cpp:1037](StCore/StWindowImpl.cpp#L1037)).
Events then reach `tryClick()` / `tryUnClick()` in the movie player ([StMoviePlayer/StMoviePlayer.cpp:1435](StMoviePlayer/StMoviePlayer.cpp#L1435)) and picture viewer ([StImageViewer/StImageViewer.cpp:1053](StImageViewer/StImageViewer.cpp#L1053)).
Configure handling reads the top-level size ([StCore/StWindowImplLin.cpp:1033](StCore/StWindowImplLin.cpp#L1033)); fullscreen has separate window management at [line 435](StCore/StWindowImplLin.cpp#L435).
The edition path needs a separate logical UI rectangle and media backing extent; treating doubled SBS pixels as UI coordinates would break hit testing.

## Per-file state and writes

Eye swap is already a field on the current item's `StStereoParams`, accessed by `StSwapLRParam` ([StGLWidgets/StGLImageRegion.cpp:22](StGLWidgets/StGLImageRegion.cpp#L22)).
The current input format field also holds the decoded format: texture upload writes `mySrcFormat` to `StereoFormat` ([StShared/StGL/StGLTextureData.cpp:953](StShared/StGL/StGLTextureData.cpp#L953)).
A persisted explicit override must be distinguishable from this detected format.

The movie player's recent entries retain parameter objects in memory, but disk serialization stores file paths and a resume timestamp above 360 seconds only ([StShared/StPlayList.cpp:929](StShared/StPlayList.cpp#L929), [line 971](StShared/StPlayList.cpp#L971)).
Its reopen handler takes only `Timestamp` from the old parameters ([StMoviePlayer/StMoviePlayer.cpp:2291](StMoviePlayer/StMoviePlayer.cpp#L2291)).
The picture viewer saves only its last left/right paths, subject to its existing preference ([StImageViewer/StImageViewer.cpp:489](StImageViewer/StImageViewer.cpp#L489)).
Input packing is also saved as an application setting, not a per-file override ([StMoviePlayer/StMoviePlayer.cpp:786](StMoviePlayer/StMoviePlayer.cpp#L786), [StImageViewer/StImageViewer.cpp:478](StImageViewer/StImageViewer.cpp#L478)).
The edition needs per-file packing/swap persistence, reopen restoration, and clearing with the owning history entry in both applications.

JPEG/JPS metadata writing is explicit: `doSaveImageInfo()` requires the affirmative action ([StImageViewer/StImageViewer.cpp:1468](StImageViewer/StImageViewer.cpp#L1468)), then [StImageViewer/StImageLoader.cpp:764](StImageViewer/StImageLoader.cpp#L764) writes JPS metadata and reloads.
The corresponding movie-player save call is commented out ([StMoviePlayer/StMoviePlayer.cpp:2282](StMoviePlayer/StMoviePlayer.cpp#L2282)).
Ordinary viewing choices must use application state, without file writes or xattrs.
Any explicit save-to-file action needs separate round-trip proof and must clear the stored override only after success.

## Edition implementation boundary

The target is the public [Sparky stereo presentation contract](https://github.com/Sparky-OS/sparky-stereo-os/blob/main/docs/how-it-works.md): one media surface containing left then right at their stored resolutions, one declaration, and ordinary undeclared UI.
Full SBS names a layout, not upscaling: half TaB 1920x1080 becomes 3840x540; half SBS stays 1920x1080.
KWin owns display scaling and every physical output mode.

1. Add an explicit edition CMake option, default off for the ordinary build, with a required `libstereo-declare >= 1.4.0` dependency when enabled.
   Gate both applications' renderer registration and output dependencies; inspect diagnostics dependencies before excluding modules from the build.
2. Put shared Linux media-surface lifetime and declaration code in a sibling module with `stereo` in its name, called by both viewers.
   The existing Xlib backend makes an Xwayland media child the first route to prototype.
   Use a non-stereo GL visual for explicitly packed SBS to avoid a second GLX declaration/packing owner.
   Declare the media child through libstereo-declare, register the child with its top level, and keep media below an alpha-capable ordinary UI surface.
   Prove child redirection, stacking, raw buffer dimensions and input before treating that arrangement as implemented.
3. Separate flat media texture drawing from UI/child drawing in `StGLImageRegion`; hand over each unpacked sample once, without the current viewport fit or filtering.
   Carry intended view size separately through `stereo_declare_x11_one_view_size()` with support version 6.
   Preserve projection, crop, pan and rotation capabilities with explicit geometry tests; a flat copy test alone cannot qualify them.
4. Add explicit per-file override state beside resume/history state, shared by the viewers, with switching, reopen and history-clear checks.
5. Keep the ordinary build as the control and provide a mono path when the required compositor contract is absent.
   Do not silently feed SBS to an undeclared window or expose the application's device output modes in the edition path.

The library's [1.4.0 API](https://github.com/Sparky-OS/libstereo-declare/blob/002a7bf/API.md) documents support discovery, declaration/removal, one-view size and Wayland protocol v2.
Its X11 helper does not manage the stereo child list; that lifecycle still belongs to the application adapter or an owning shared layer.
Declaration, child redirection, UI alpha and pointer mapping must be checked together on the real patched Xwayland/KWin stack before expanding the implementation.

## Acceptance gates for subsequent milestones

The first implementation gate is a labelled flat picture in a separate media surface, with a menu overlapping it, tested in a fresh isolated headless KWin session.
Record the media child's raw X geometry and pixels, declaration ownership, both compositor eyes, UI equality, and a known logical click target.
The unchanged application must fail the declaration/separation expectation; a deliberately swapped expected eye must fail the eye checker.
This audit has not run either control.

Then cover resize, fullscreen, stereo/mono transitions, per-file swap, close/reopen, and an old/absent-support mono fallback.
Use MPO, JPS, marked PNG/PNS, explicit SBS JPEG, SEI and Matroska videos, full/half SBS and TaB, with both orders and timed frame labels.
Compare the handed-over samples against independently generated address fixtures before measuring KWin's resize.
Run the ordinary build and edition mono control with deterministic UI state.
The paired-output gate comes after single-surface proof: one virtual GPU with two outputs, then two virtual GPUs, each connector captured separately, output eye swap and wrong-eye controls.
Application dual-window output is not evidence for KWin pairing.
Package and installed-package tests follow those gates; no package or hardware qualification is established by this audit.
