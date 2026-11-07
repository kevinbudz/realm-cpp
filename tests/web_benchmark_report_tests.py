#!/usr/bin/env python3
"""Smoke the downloaded browser benchmark report against representative logs."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/web_benchmark_report.py"


def write_run(directory, stem, api, cpu, fallback=None, browser="Mock Browser/1.0",
              scenario="S1-mixed-dense", drawable=(1280, 720), include_gpu=True):
    renderer = "WebGPU" if api == "webgpu" else "OpenGL"
    metadata = {
        "renderer": renderer,
        "manifest": "/realm-bench/S1-mixed-dense.json",
        "GL_vendor": "Mock Vendor",
        "GL_renderer": "Mock Adapter",
        "GL_version": "WebGPU" if api == "webgpu" else "OpenGL ES 3.0 (WebGL 2.0)",
    }
    if fallback:
        metadata["renderer_fallback"] = fallback
    counters = {
        "draw_calls": 12 if api == "webgpu" else 14,
        "webgpu_stream_allocations": 1 if api == "webgpu" else 0,
        "webgpu_stream_upload_calls": 2 if api == "webgpu" else 0,
        "webgpu_stream_upload_bytes": 384 if api == "webgpu" else 0,
    }
    rows = [{"type": "metadata", "key": key, "value": value} for key, value in metadata.items()]
    for frame_id in range(3):
        rows.append({
            "type": "frame", "frame_id": frame_id, "warmup": frame_id == 0,
            "capture": False, "interval_valid": frame_id > 0, "interval_ms": 16.67,
            "cpu_ms": cpu, "present_ms": 0.25, "drawable_width": drawable[0],
            "drawable_height": drawable[1], "counters": counters,
        })
    if include_gpu:
        for frame_id in (1, 2):
            rows.append({"type": "gpu", "frame_id": frame_id, "normal": True, "total_ms": 1.5})
    telemetry = directory / f"{stem}-telemetry.jsonl"
    telemetry.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
    environment = {
        "schema": "realm-web-benchmark-environment-v1",
        "scenario": scenario,
        "requested_renderer": "WebGPU" if api == "webgpu" else "WebGL2",
        "actual_renderer": renderer,
        "frames": 3,
        "warmup_frames": 1,
        "runtime_errors": [],
        "browser_user_agent": browser,
        "device_pixel_ratio": 1,
        "viewport": {"width": drawable[0], "height": drawable[1]},
    }
    telemetry.with_name(f"{stem}-environment.json").write_text(json.dumps(environment))
    return telemetry


with tempfile.TemporaryDirectory(prefix="realm-web-bench-report-") as temp:
    directory = Path(temp)
    gl = write_run(directory, "gl-1", "webgl2", 2.0)
    gpu = write_run(directory, "gpu-1", "webgpu", 1.5)
    output = directory / "report.md"
    result = subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(gpu), "--output", str(output)],
        check=True, capture_output=True, text=True,
    )
    assert output.is_file() and output.with_suffix(".json").is_file()
    assert "Active CPU ratio (WebGL2 ÷ WebGPU)" in result.stdout
    assert "queue.writeBuffer" in output.read_text()
    assert output.read_text().count("webgpu_stream_upload_bytes") == 1
    result_json = json.loads(output.with_suffix(".json").read_text())
    assert result_json["webgl2"][0]["metrics"]["active_cpu_ms"]["median"] == 2.25
    assert result_json["webgpu"][0]["metrics"]["active_cpu_ms"]["median"] == 1.75

    no_timer = write_run(directory, "gpu-no-timer", "webgpu", 1.5, include_gpu=False)
    subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(no_timer), "--output", str(output)],
        check=True, capture_output=True, text=True,
    )
    result_json = json.loads(output.with_suffix(".json").read_text())
    assert result_json["webgpu"][0]["gpu_frames"] == 0
    assert result_json["webgpu"][0]["metrics"]["gpu_total_ms"] is None

    other_browser = write_run(directory, "gpu-other-browser", "webgpu", 1.5,
                              browser="Different Browser/1.0")
    subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(other_browser), "--output", str(output)],
        check=True, capture_output=True, text=True,
    )
    assert "runs differ in browser user agent" in output.read_text()

    other_fixture = write_run(directory, "gpu-other-fixture", "webgpu", 1.5, scenario="ui")
    mismatch = subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(other_fixture), "--output", str(output)],
        capture_output=True, text=True,
    )
    assert mismatch.returncode != 0 and "different fixtures" in mismatch.stderr

    other_size = write_run(directory, "gpu-other-size", "webgpu", 1.5, drawable=(1366, 768))
    mismatch = subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(other_size), "--output", str(output)],
        capture_output=True, text=True,
    )
    assert mismatch.returncode != 0 and "different drawable sizes" in mismatch.stderr

    gpu_error = write_run(directory, "gpu-error", "webgpu", 1.5)
    error_environment = json.loads(gpu_error.with_name("gpu-error-environment.json").read_text())
    error_environment["runtime_errors"] = ["WebGPU error (1): validation failure"]
    gpu_error.with_name("gpu-error-environment.json").write_text(json.dumps(error_environment))
    rejected = subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(gpu_error), "--output", str(output)],
        capture_output=True, text=True,
    )
    assert rejected.returncode != 0 and "runtime errors make this sample invalid" in rejected.stderr

    wrong_api = write_run(directory, "gpu-fallback", "webgpu", 1.5, fallback="adapter unavailable")
    rejected = subprocess.run(
        ["python3", str(TOOL), "--webgl2", str(gl), "--webgpu", str(wrong_api), "--output", str(output)],
        capture_output=True, text=True,
    )
    assert rejected.returncode != 0 and "fallback" in rejected.stderr

print("Web benchmark report: timings, timer gaps, API/fallback, fixture/size and environment checks pass")
