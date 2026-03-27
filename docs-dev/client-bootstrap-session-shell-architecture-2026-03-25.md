# Client Bootstrap Session Shell Architecture

Date: 2026-03-25

Task IDs: `DV-08-T12`

## Summary

Quake Champions on Steam does not appear to use an in-install self-updater for
its game binaries. Steam owns depot/build delivery before launch, while the
game itself behaves like a branded runtime shell that handles session startup,
account login, entitlement sync, and menu/web surfaces.

WORR should copy that session-shell behavior, not Steam's depot patching model.
The best follow-on architecture is:

- keep `worr_x86_64(.exe)` as the permanent client shell
- keep `worr_engine_x86_64(.dll/.so/.dylib)` as the hosted engine payload
- move update/auth/news UX into that shell instead of treating updater UI as a
  separate front-end phase
- make updates manifest-driven so the shell synchronizes the local install to
  the authoritative release state, not just "installs an update package"
- keep `worr_updater_x86_64(.exe)` only for the cases that truly require an
  external apply worker: locked file replacement, elevation, and post-apply
  relaunch

The right abstraction is not a literal VM. It is a stable host process with a
versioned engine boundary and a minimal out-of-process apply path.

## What Was Observed In The Local Quake Champions Install

Inspection date: 2026-03-25

### Steam owns the shipped build state

The local Steam manifest at
`C:\Program Files (x86)\Steam\steamapps\appmanifest_611500.acf` records:

- `LauncherPath` as `C:\Program Files (x86)\Steam\steam.exe`
- `buildid` and `TargetBuildID` as `21821932`
- download/staging byte counters maintained by Steam
- installed depot `611501` with manifest `3290374253057709896`

That is Steam depot/build state, not the state model of a custom in-install
patcher.

### The install tree does not expose a standalone game patcher

The shipped client under
`C:\Program Files (x86)\Steam\steamapps\common\quakechampions\client\bin\pc`
contains:

- `QuakeChampions.exe`
- `steam_api64.dll`
- `libcef.dll`
- `web_browser_appRelease.exe`
- Chromium `.pak` resources and locales
- media/runtime DLLs such as Bink and FMOD

It does not expose a visible game-side updater executable, staged update
directory, or local install manifest comparable to WORR's current updater
contract.

### Chromium/browser support is present

The install also contains Chromium Embedded Framework support files under:

- `client\bin\pc\libcef.dll`
- `client\prebuild\chromium\*.pak`
- `client\bin\pc\web_browser_appRelease.exe`

That strongly suggests an embedded browser subprocess model for login, menu, or
web-driven surfaces inside the game shell.

### Game-side logs show identity/session work, not file patching

The local QC client data under
`C:\Users\djdac\AppData\Local\id Software\Quake Champions\client\`
contains `bnet.log` and `sandbox.json`.

Observed behavior in `bnet.log`:

- Steam token exchange
- Bethesda account/session login
- entitlement sync
- telemetry/session lifecycle calls

No equivalent patch-manifest fetch, file hash pass, archive extraction, or
install-apply sequence was observed in that log.

## Conclusion About Quake Champions

Quake Champions on Steam is best understood as:

1. platform-managed binary/content delivery through Steam depots
2. a branded runtime shell that owns login/session startup
3. embedded browser support for web-facing surfaces

That is materially different from a traditional self-updating launcher that
downloads and replaces its own install tree before the main game starts.

## What WORR Should Copy And What It Should Not Copy

### Copy

- a single branded client shell process
- seamless pre-game UX inside the same shell
- session/auth/news/update presentation owned by the client shell
- a narrow engine hosting boundary behind the public executable

### Do not copy

- Steam's depot/build delivery model for standalone installs
- a permanently visible separate updater executable/window
- native binary hot-swap while the active engine library is still executing
- a literal VM abstraction for the main game process

## Recommended WORR Architecture

## 1. Keep the existing hosted-engine model

WORR already has the correct high-level shape:

- `worr_x86_64(.exe)` is the public client bootstrap host
- `worr_engine_x86_64(.dll/.so/.dylib)` is loaded in-process
- `worr_updater_x86_64(.exe)` is already reserved for approved file
  replacement and relaunch

That should be extended, not replaced.

## 2. Promote the client bootstrap into a long-lived session shell

The client bootstrap should stop being only a splash/update prelude and become
the permanent shell for the whole client session.

Shell responsibilities should include:

- startup branding/splash
- update discovery and approval
- update progress UI
- optional auth/news/service surfaces
- engine load and relaunch control
- crash/restart recovery decisions

The engine then becomes one hosted module inside that shell.

## 3. Keep the updater as a module in the shell, not as the visible app

Update orchestration should run in-process inside the shell:

- manifest lookup
- version comparison
- package download
- hash validation
- user prompts
- progress/status UI

The external worker should remain for only the parts that genuinely need a
different process:

- replacing files currently locked by the running install
- requesting elevation
- relaunching the updated public bootstrap after apply

## 4. Make the shell own the window/fullscreen lifecycle

This is the key change required for the "same window/fullscreen" requirement.

Today, WORR still uses a temporary bootstrap window and then lets the engine
create its own main window later:

- `src/updater/bootstrap.cpp` creates the splash window and dismisses it on
  engine handoff
- `src/unix/video/sdl.c` currently calls `SDL_CreateWindow(...)` inside the
  engine video path
- `src/windows/client.c` currently calls `CreateWindowA(...)` for the engine's
  Win32 main window

That means the current architecture is in-process, but not yet same-window.

To become same-window and same-fullscreen:

- the bootstrap shell must own the native window for the lifetime of the client
- the engine must attach to a host-provided window/surface instead of always
  creating its own
- shell scenes and engine rendering must hand off within the same display
  object, not by destroying one window and creating another

This is the real equivalent of the seamless-shell experience the QC install
suggests.

## 5. Treat engine updates and content updates differently

WORR should separate:

- code updates
  - bootstrap executable
  - engine library
  - renderer/shared libraries
- content updates
  - packaged assets
  - data packs
  - future optional downloadable content

Content packages can be downloaded, verified, and staged while the shell is
alive. Engine-code changes still need a controlled restart/apply boundary.

That yields a more modern user experience without pretending native engine code
can be swapped safely in-place mid-execution.

## 6. Make update apply a synchronization contract

The bootstrap shell should synchronize the local installation to the current
authoritative release manifest for the user's platform, role, and channel.

That means the updater contract is not merely:

- download one ZIP
- extract it
- hope the live tree now matches the release

It should instead be:

- load the local install manifest
- fetch the remote release/index manifest
- compute the exact delta between local and remote managed files
- stage only the missing or outdated payloads
- remove managed files that no longer belong to the release
- preserve explicitly user-owned paths
- write the new local manifest last

### Required synchronization rules

Managed install state should converge to the remote manifest:

- files present remotely and missing locally must be added
- files present in both but with different hashes/sizes must be replaced
- files tracked locally but absent remotely must be removed if they are
  managed release files
- files on the preserve allowlist must survive synchronization even if they are
  not part of the shipped payload

That last rule is critical for:

- config files
- save data
- screenshots
- demos
- logs
- future locally cached/downloaded user content

### Why this matters

Without explicit synchronization semantics, updater systems tend to drift into
partial-overlay behavior:

- stale DLLs stay behind after renames
- removed assets remain on disk and mask packaging errors
- rollback/recovery becomes ambiguous
- "repair install" and "verify files" become different code paths

A manifest-convergence model solves those problems and gives WORR three useful
operations through one backend:

- normal update
- install repair/self-heal
- first-run/bootstrap install completion

### Recommended shape

For standalone WORR installs:

- keep `worr_install_manifest.json` as the local authoritative inventory
- treat the release manifest as the desired target state
- let the shell compute a sync plan before any apply step
- let the worker execute only the file mutations that cannot happen safely in
  the live shell process

For future store-backed builds:

- defer managed executable/content synchronization to the platform when the
  platform already owns the install tree
- keep the shell-side sync logic for WORR-owned data outside the platform's
  patch domain only if needed

The practical result is that WORR should behave like a converging installer:

- the shell decides what the install should look like
- the updater synchronizes the live tree to that state
- the worker exists only to perform the unsafe file replacement boundary

## Why A Literal "Bootstrapper VM" Is The Wrong Frame

The useful part of the VM idea is stable hosting. The risky part is taking that
too literally.

A true VM-style abstraction would imply live code isolation and hot-swapping of
large native runtime components. For WORR's current engine architecture, that is
the wrong tradeoff:

- it increases complexity across render, input, audio, and platform code
- it makes crash ownership and native resource lifetime harder
- it does not remove the need for an external file-apply boundary

The better model is:

- stable shell process
- hosted engine library with a versioned ABI
- controlled shutdown/reload when code changes require it

## Recommended Phasing

## Phase 1: session-shell refactor over the current bootstrap

- keep the existing worker and release-index flow
- replace the one-shot splash with a client shell state machine:
  - splash
  - checking
  - update required
  - downloading
  - ready to launch
  - restarting after apply
- keep engine hosting in-process

## Phase 2: host-owned window contract

- add a bootstrap-to-engine window ownership contract
- teach the engine video path to adopt a host-provided native window
- preserve fullscreen/display mode across shell-to-engine handoff

This is the milestone that actually delivers the same-window requirement.

## Phase 3: optional shell modules

- add optional auth/news/web surfaces only if they solve a real need
- keep them modular so standalone desktop builds do not depend on a heavy web
  stack unless required

If a browser surface is needed, treat it like QC appears to: a helper/runtime
module behind the main shell, not a separate launcher application.

## Phase 4: split platform update adapters

- standalone builds continue using WORR's release index + apply worker
- Steam builds should defer executable/content patching to Steam instead of
  fighting platform policy
- future stores can provide store-specific adapters behind the same shell UX

## Best Approach

The best approach for WORR is to evolve the existing bootstrap host into a
permanent client session shell, not to build a separate modern-looking launcher
beside it and not to attempt a literal VM.

Concretely:

- keep the hosted engine library model
- move update orchestration into the shell as an internal module
- make updates converge the local install to the authoritative manifest instead
  of relying on package-overlay semantics
- keep a tiny external worker for file apply/elevation only
- refactor window ownership so the shell and engine share one native
  window/fullscreen lifecycle
- separate content staging from code-apply/restart paths

That matches what is valuable in the Quake Champions experience while fitting
WORR's current architecture and cross-platform updater contract.
