# FR-10-T14 headless mouse backend fail-closed guard

Date: 2026-07-19  
Project task: `FR-10-T14`

## Change

`win_headless 1` already made client input initialization fail closed. The
Windows platform mouse backend now enforces that policy independently: it
does not register raw mouse input, acquire capture, clip the cursor, or warp
the pointer for a headless surface.

This defence is intentionally below `IN_Init()`. Window positioning, resize,
focus, and future renderer code can reach Windows mouse helpers separately
from normal client input startup. Guarding those helpers means no such path can
claim or move the developer's desktop pointer during an automated capture.

If headless mode is selected after a mouse was already acquired, a subsequent
grab request releases its capture and cursor clip state before returning.

## Validation

Run the source-level contract without launching a client window:

```powershell
python -m unittest tools/networking/test_headless_input_contract.py
```

The contract verifies both layers: `IN_Init()` bypasses platform mouse setup,
and the Windows backend places the headless rejection before raw registration,
acquisition, cursor clipping, and pointer warping.
