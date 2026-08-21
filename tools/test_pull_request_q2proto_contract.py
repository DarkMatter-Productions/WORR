from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "pull-request.yml"
UPLOAD_ARTIFACT_PIN = (
    "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
)
CHECKOUT_PIN = "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd"


def _job_block(text: str, job_name: str) -> str:
    marker = f"\n  {job_name}:\n"
    start = text.index(marker) + 1
    next_job = text.find("\n  ", start + len(marker))
    while next_job >= 0:
        line_end = text.find("\n", next_job + 1)
        candidate = text[next_job + 1 : line_end if line_end >= 0 else None]
        if (
            candidate.startswith("  ")
            and not candidate.startswith("    ")
            and candidate.endswith(":")
        ):
            return text[start:next_job]
        next_job = text.find("\n  ", next_job + 3)
    return text[start:]


def test_required_q2proto_job_runs_the_complete_bounded_gate() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    block = _job_block(text, "q2proto-compatibility")

    assert "name: q2proto compatibility" in block
    assert "runs-on: ubuntu-24.04" in block
    assert "timeout-minutes: 10" in block
    assert "continue-on-error" not in block
    assert "CC: gcc" in block
    assert "tools/run_q2proto_compatibility.py" in block
    assert "--vendor-dir q2proto" in block
    assert "--source-dir q2proto/tests" in block
    assert "--timeout-seconds 300" in block
    assert "--jobs 2" in block


def test_q2proto_results_are_uploaded_even_when_the_gate_fails() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    block = _job_block(text, "q2proto-compatibility")

    assert UPLOAD_ARTIFACT_PIN in block
    assert "if: always()" in block
    assert "path: .tmp/q2proto-compatibility-results/" in block
    assert "if-no-files-found: error" in block
    assert "retention-days: 14" in block


def test_q2proto_gate_uses_the_hash_locked_meson_toolchain() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    block = _job_block(text, "q2proto-compatibility")

    assert "tools/release/build-tool-requirements.txt" in block
    assert "--require-hashes" in block
    assert 'echo "${RUNNER_TEMP}/q2proto-meson-venv/bin" >> "$GITHUB_PATH"' in block


def test_every_checkout_is_pinned_and_does_not_persist_credentials() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    lines = text.splitlines()
    checkout_indexes = [
        index for index, line in enumerate(lines) if line == "      - name: Checkout"
    ]

    assert checkout_indexes
    assert text.count(CHECKOUT_PIN) == len(checkout_indexes)
    for index in checkout_indexes:
        end = next(
            (
                candidate
                for candidate in range(index + 1, len(lines))
                if lines[candidate].startswith("      - name:")
            ),
            len(lines),
        )
        block = "\n".join(lines[index:end])
        assert CHECKOUT_PIN in block
        assert "persist-credentials: false" in block
