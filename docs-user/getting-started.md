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

4. Launch from `.install/`:
   - Windows: `.install/worr_x86_64.exe`
   - Linux/macOS: `.install/worr_x86_64`

## Published Release Layout

- Client releases launch from `worr_x86_64(.exe)` and keep runtime data in `basew/`.
- Dedicated server releases launch from `worr_ded_x86_64(.exe)` and keep configs, maps, and WORR assets in `basew/`.

## First Run Checklist

- Local build: put your Quake II data in `.install/basew/` (or point `basedir` at a valid data tree).
- Local build: keep `.install/basew/pak0.pkz` in place.
- Published release: keep runtime data under `basew/`, including `basew/pak0.pkz`.
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
- Startup crash after pull: rebuild, then rerun `tools/refresh_install.py`.
