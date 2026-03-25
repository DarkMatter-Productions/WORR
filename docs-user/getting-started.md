# Getting Started

This guide is the quick path to running WORR with your own Quake II data.

## What You Need

- A built WORR workspace (or a release package).
- Quake II data files (`baseq2/pak*.pak` from your source install, copied into `basew/`).
- A fresh staged runtime in `.install/` if you are building locally.

## Local Build Flow (Fast Path)

1. Configure:

   ```bash
   meson setup builddir --wrap-mode=forcefallback --buildtype=release -Dbase-game=basew -Ddefault-game=basew -Dtests=false
   ```

2. Compile:

   ```bash
   meson compile -C builddir
   ```

3. Refresh the local runtime package:

   ```bash
   python3 tools/refresh_install.py --build-dir builddir --install-dir .install --base-game basew
   ```

   Existing local Quake II data already placed in `.install/basew/` is
   preserved by refresh; you do not need to recopy `pak*.pak` after each build.

4. Launch from `.install/`:
   - Client bootstrap executable:
     - Windows: `.install/worr_x86_64.exe`
     - Linux/macOS: `.install/worr_x86_64`
   - Dedicated bootstrap executable:
     - Windows: `.install/worr_ded_x86_64.exe`
     - Linux/macOS: `.install/worr_ded_x86_64`

## Published Release Layout

- `worr_x86_64(.exe)` is the user-facing client bootstrap executable.
- `worr_ded_x86_64(.exe)` is the user-facing dedicated bootstrap executable.
- `worr_engine_x86_64(.dll/.so/.dylib)` is the hosted client engine library.
- `worr_ded_engine_x86_64(.dll/.so/.dylib)` is the hosted dedicated engine library.
- `worr_updater_x86_64(.exe)` is the updater/apply worker used for safe file replacement during updates.
- Runtime data stays in `basew/`.

## First Run Checklist

- Local build: put your Quake II data in `.install/basew/` (or point `basedir` at a valid data tree).
- Local build: keep `.install/basew/pak0.pkz` in place.
- Local build: `worr_update.json` and `worr_install_manifest.json` are optional. If they are absent, the bootstrap executable detects a developer install and loads the engine library directly.
- Published release: keep runtime data under `basew/`, including `basew/pak0.pkz`.
- Published release: keep `worr_update.json` and `worr_install_manifest.json` next to the bootstrap/updater executables so auto-update can run.
- If the game boots to console only, check renderer selection and your GPU driver.

## Useful Start Arguments

```text
+set basedir <path>
+set r_renderer opengl
+set r_renderer vulkan
+set r_renderer rtx
```

On macOS, `r_renderer vulkan` uses SDL3 + MoltenVK. `r_renderer rtx` is not
expected to work on current macOS Vulkan stacks.

## If Something Feels Off

- No sound: verify OpenAL device output and in-game volume cvars.
- Missing UI/textures: confirm `pak0.pkz` exists under `.install/basew/` for local builds or under `basew/` in a published release.
- Startup crash after pull: rebuild, then rerun `tools/refresh_install.py` so the bootstrap executables and engine libraries are refreshed together.
