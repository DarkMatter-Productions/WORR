# VS Code Bootstrap Debug Presets

Superseded by `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.

This note covered a short-lived workaround where the tracked VS Code debug
presets targeted `bin/worr_runtime_*` directly because the public `worr_*`
names had been repurposed as launcher stubs. The later launcher/runtime layout
revision restored `worr_x86_64` and `worr_ded_x86_64` as the real runtimes and
moved update-aware startup onto explicit launcher binaries instead.
