#!/usr/bin/env python3
"""Shared process policy for isolated, headless networking runtime gates.

Every gate launches without a visible console and with standard input detached.
On Windows, terminating a direct launcher handle is insufficient: child engine
or renderer processes can outlive it. The cleanup helper therefore closes the
entire isolated process tree and waits for the root before a gate reports that
its run is complete.
"""

from __future__ import annotations

import ctypes
import os
import subprocess
from typing import Any


JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
_WINDOWS_JOBS: dict[int, int] = {}


class _JobObjectBasicLimitInformation(ctypes.Structure):
    _fields_ = (
        ("per_process_user_time_limit", ctypes.c_longlong),
        ("per_job_user_time_limit", ctypes.c_longlong),
        ("limit_flags", ctypes.c_ulong),
        ("minimum_working_set_size", ctypes.c_size_t),
        ("maximum_working_set_size", ctypes.c_size_t),
        ("active_process_limit", ctypes.c_ulong),
        ("affinity", ctypes.c_size_t),
        ("priority_class", ctypes.c_ulong),
        ("scheduling_class", ctypes.c_ulong),
    )


class _IoCounters(ctypes.Structure):
    _fields_ = (
        ("read_operation_count", ctypes.c_ulonglong),
        ("write_operation_count", ctypes.c_ulonglong),
        ("other_operation_count", ctypes.c_ulonglong),
        ("read_transfer_count", ctypes.c_ulonglong),
        ("write_transfer_count", ctypes.c_ulonglong),
        ("other_transfer_count", ctypes.c_ulonglong),
    )


class _JobObjectExtendedLimitInformation(ctypes.Structure):
    _fields_ = (
        ("basic_limit_information", _JobObjectBasicLimitInformation),
        ("io_info", _IoCounters),
        ("process_memory_limit", ctypes.c_size_t),
        ("job_memory_limit", ctypes.c_size_t),
        ("peak_process_memory_used", ctypes.c_size_t),
        ("peak_job_memory_used", ctypes.c_size_t),
    )


def creation_flags() -> int:
    """Return the no-window launch flag when the gate runs on Windows."""
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def _close_windows_job(pid: int) -> bool:
    """Close a tracked job; Windows destroys every member on close."""
    handle = _WINDOWS_JOBS.pop(pid, None)
    if handle is None:
        return False
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CloseHandle(ctypes.c_void_p(handle))
    return True


def _track_windows_process_tree(process: subprocess.Popen[Any]) -> None:
    """Assign an isolated launch to a kill-on-close job before it can outlive us."""
    if os.name != "nt":
        return
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.argtypes = (ctypes.c_void_p, ctypes.c_wchar_p)
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    kernel32.SetInformationJobObject.argtypes = (
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_ulong,
    )
    kernel32.SetInformationJobObject.restype = ctypes.c_int
    kernel32.AssignProcessToJobObject.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
    kernel32.AssignProcessToJobObject.restype = ctypes.c_int
    kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)
    kernel32.CloseHandle.restype = ctypes.c_int

    handle = kernel32.CreateJobObjectW(None, None)
    if not handle:
        raise OSError(ctypes.get_last_error(), "CreateJobObjectW failed")
    information = _JobObjectExtendedLimitInformation()
    information.basic_limit_information.limit_flags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    try:
        if not kernel32.SetInformationJobObject(
            handle,
            JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
            ctypes.byref(information),
            ctypes.sizeof(information),
        ):
            raise OSError(ctypes.get_last_error(), "SetInformationJobObject failed")
        process_handle = getattr(process, "_handle", None)
        if process_handle is None or not kernel32.AssignProcessToJobObject(
            handle, ctypes.c_void_p(process_handle)
        ):
            raise OSError(ctypes.get_last_error(), "AssignProcessToJobObject failed")
    except Exception:
        kernel32.CloseHandle(handle)
        raise
    _WINDOWS_JOBS[process.pid] = int(handle)


def start_headless_process(
    *popen_args: Any, **popen_kwargs: Any
) -> subprocess.Popen[Any]:
    """Launch one isolated input-free runtime process and track its descendants."""
    if popen_kwargs.get("stdin") is not subprocess.DEVNULL:
        raise ValueError("headless runtime launches must set stdin=subprocess.DEVNULL")
    flags = creation_flags()
    if popen_kwargs.get("creationflags", flags) != flags:
        raise ValueError("headless runtime launches must use the no-window creation flags")
    popen_kwargs["creationflags"] = flags
    process = subprocess.Popen(*popen_args, **popen_kwargs)
    try:
        _track_windows_process_tree(process)
    except Exception:
        # Do not continue a run with a process whose descendants could escape
        # the isolated harness. The direct handle is still available here.
        terminate_process_tree(process)
        raise
    return process


def terminate_process_tree(process: subprocess.Popen[Any] | None) -> bool:
    """Stop a still-running isolated test launch and all its Windows children."""
    if process is None:
        return False

    if os.name == "nt":
        # A game launcher can retain engine descendants while its direct handle
        # exits. taskkill /T keeps no headless client, engine, renderer, or
        # dedicated child alive after the harness has completed. CREATE_NO_WINDOW
        # makes taskkill itself non-visible and it inherits no interactive input.
        running = process.poll() is None
        tracked = process.pid in _WINDOWS_JOBS
        if not running and not tracked:
            return False
        try:
            if running:
                try:
                    subprocess.run(
                        ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False,
                        timeout=5,
                        creationflags=creation_flags(),
                    )
                except subprocess.TimeoutExpired:
                    pass
            if running:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        finally:
            _close_windows_job(process.pid)
        return True

    if process.poll() is not None:
        return False

    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    return True
