#!/usr/bin/env python3
"""Compare downloaded browser benchmark telemetry from WebGL2 and WebGPU runs.

Example:
  python3 tools/web_benchmark_report.py --webgl2 gl-1-telemetry.jsonl gl-2-telemetry.jsonl \
      --webgpu gpu-1-telemetry.jsonl gpu-2-telemetry.jsonl --output build/web-bench/report.md

The shell exports a sibling *-environment.json file. Samples with renderer fallback or an
unexpected active API are rejected, so a requested WebGPU run that fell back to WebGL2 cannot
appear as a WebGPU result.
"""
import argparse
import json
import math
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {
        "samples": len(values),
        "median": statistics.median(values),
        "p95": values[max(0, math.ceil(0.95 * len(values)) - 1)],
        "p99": values[max(0, math.ceil(0.99 * len(values)) - 1)],
    }


def environment_path(path):
    suffix = "-telemetry"
    stem = path.stem[:-len(suffix)] if path.stem.endswith(suffix) else path.stem
    return path.with_name(stem + "-environment.json")


def read_run(path, expected_api):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    metadata, frames, gpu = {}, {}, []
    for row in rows:
        kind = row.get("type")
        if kind == "metadata":
            metadata[row["key"]] = row["value"]
        elif kind == "frame":
            frame_id = row["frame_id"]
            if frame_id in frames:
                raise ValueError(f"{path}: duplicate frame {frame_id}")
            frames[frame_id] = row
        elif kind == "gpu":
            gpu.append(row)

    if not frames:
        raise ValueError(f"{path}: no frame records")
    expected_label = "WebGPU" if expected_api == "webgpu" else "OpenGL"
    actual = metadata.get("renderer")
    if actual != expected_label or metadata.get("renderer_fallback"):
        fallback = metadata.get("renderer_fallback")
        raise ValueError(
            f"{path}: expected active {expected_label}, got {actual or 'unknown'}"
            + (f" (fallback: {fallback})" if fallback else "")
        )

    measured = [f for f in frames.values() if not f["warmup"] and not f["capture"]]
    if not measured:
        raise ValueError(f"{path}: no measured non-warmup frames")
    frame_dimensions = {
        (f["drawable_width"], f["drawable_height"]) for f in measured
    }
    if len(frame_dimensions) != 1:
        raise ValueError(f"{path}: drawable size changed during the measured run")
    measured_ids = {f["frame_id"] for f in measured}
    normal_gpu = [g for g in gpu if g["normal"] and g["frame_id"] in measured_ids]
    counters = sorted({name for f in measured for name in f.get("counters", {})})
    counter_stats = {
        name: distribution([f.get("counters", {}).get(name, 0) for f in measured])
        for name in counters
    }
    metrics = {
        "active_cpu_ms": distribution([f["cpu_ms"] + f["present_ms"] for f in measured]),
        "cpu_ms": distribution([f["cpu_ms"] for f in measured]),
        "present_ms": distribution([f["present_ms"] for f in measured]),
        "frame_interval_ms": distribution(
            [f["interval_ms"] for f in measured if f["interval_valid"]]
        ),
        "gpu_total_ms": distribution([g["total_ms"] for g in normal_gpu]),
        "counters": counter_stats,
    }
    env_path = environment_path(path)
    environment = json.loads(env_path.read_text()) if env_path.is_file() else None
    if environment:
        runtime_errors = environment.get("runtime_errors", [])
        if runtime_errors:
            raise ValueError(f"{env_path}: runtime errors make this sample invalid: {runtime_errors}")
        if environment.get("actual_renderer") not in ("OpenGL", "WebGPU"):
            raise ValueError(f"{env_path}: missing active renderer")
        if environment["actual_renderer"] != actual:
            raise ValueError(f"{env_path}: active renderer disagrees with telemetry")
        requested = environment.get("requested_renderer")
        expected_environment = "WebGPU" if expected_api == "webgpu" else "WebGL2"
        if requested != expected_environment:
            raise ValueError(f"{env_path}: requested renderer is {requested}, expected {expected_environment}")

    scenario = (environment or {}).get("scenario")
    if not scenario:
        manifest = metadata.get("manifest", "")
        scenario = Path(manifest).name.removesuffix(".json") or "unknown"
    dimensions = [measured[-1]["drawable_width"], measured[-1]["drawable_height"]]
    return {
        "source": str(path),
        "api": expected_api,
        "scenario": scenario,
        "frames": len(frames),
        "warmup_frames": sum(1 for f in frames.values() if f["warmup"]),
        "measured_frames": len(measured),
        "gpu_frames": len(normal_gpu),
        "drawable": dimensions,
        "metadata": metadata,
        "environment": environment,
        "metrics": metrics,
    }


def median_run_metric(runs, metric, percentile="median"):
    values = [run["metrics"][metric][percentile] for run in runs if run["metrics"][metric]]
    return statistics.median(values) if values else None


def fmt(value, digits=3):
    return "—" if value is None else f"{value:.{digits}f}"


def report(webgl2, webgpu):
    all_runs = webgl2 + webgpu
    scenarios = {run["scenario"] for run in all_runs}
    dimensions = {tuple(run["drawable"]) for run in all_runs}
    frames = {(run["frames"], run["warmup_frames"]) for run in all_runs}
    if len(scenarios) != 1:
        raise ValueError(f"Runs use different fixtures: {sorted(scenarios)}")
    if len(dimensions) != 1:
        raise ValueError(f"Runs use different drawable sizes: {sorted(dimensions)}")
    if len(frames) != 1:
        raise ValueError(f"Runs use different total/warmup frame counts: {sorted(frames)}")
    scenario = next(iter(scenarios))
    width, height = next(iter(dimensions))
    environments = [run["environment"] for run in all_runs if run["environment"]]
    environment_warnings = []
    if len(environments) != len(all_runs):
        environment_warnings.append(
            f"{len(all_runs) - len(environments)} run(s) lack the browser environment sidecar"
        )
    for key, label in (("browser_user_agent", "browser user agent"),
                       ("device_pixel_ratio", "device pixel ratio"),
                       ("viewport", "browser viewport")):
        values = {json.dumps(env.get(key), sort_keys=True) for env in environments if env.get(key) is not None}
        if len(values) > 1:
            environment_warnings.append(f"runs differ in {label}")
    lines = [
        "# Browser renderer benchmark",
        "",
        f"Fixture: **{scenario}** · Drawable: **{width}×{height}** · "
        f"Frames: **{next(iter(frames))[0]}** ({next(iter(frames))[1]} warmup)",
        "",
        "Only logs whose telemetry confirms the requested active renderer are accepted. "
        "The browser loop is paced by `requestAnimationFrame`; frame intervals include that pacing "
        "and are not used as uncapped throughput. Active CPU work is `cpu_ms + present_ms` "
        "(event/build/submit plus renderer finish/present), which excludes the idle gap between frames.",
        "",
        ("Environment check: **" + "; ".join(environment_warnings) + ".**")
        if environment_warnings else "Environment check: browser, viewport and pixel ratio match across sidecars.",
        "",
        "| Renderer | Runs | Active CPU median / p95 (ms) | Build CPU p95 (ms) | "
        "Finish/present p95 (ms) | RAF interval median (ms) | GPU total median / p95 (ms) | "
        "GPU frames / measured | Draw calls / frame |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    groups = [("WebGL2", webgl2), ("WebGPU", webgpu)]
    for label, runs in groups:
        active_median = median_run_metric(runs, "active_cpu_ms", "median")
        active_p95 = median_run_metric(runs, "active_cpu_ms", "p95")
        cpu_p95 = median_run_metric(runs, "cpu_ms", "p95")
        present_p95 = median_run_metric(runs, "present_ms", "p95")
        interval = median_run_metric(runs, "frame_interval_ms", "median")
        gpu_median = median_run_metric(runs, "gpu_total_ms", "median")
        gpu_p95 = median_run_metric(runs, "gpu_total_ms", "p95")
        gpu_count = sum(run["gpu_frames"] for run in runs)
        measured = sum(run["measured_frames"] for run in runs)
        draw_calls = [run["metrics"]["counters"].get("draw_calls") for run in runs]
        draw_values = [s["median"] for s in draw_calls if s]
        draw_median = statistics.median(draw_values) if draw_values else None
        lines.append(
            f"| {label} | {len(runs)} | {fmt(active_median)} / {fmt(active_p95)} | "
            f"{fmt(cpu_p95)} | {fmt(present_p95)} | {fmt(interval)} | "
            f"{fmt(gpu_median)} / {fmt(gpu_p95)} | {gpu_count} / {measured} | {fmt(draw_median, 1)} |"
        )

    gl_active = median_run_metric(webgl2, "active_cpu_ms", "median")
    gpu_active = median_run_metric(webgpu, "active_cpu_ms", "median")
    gl_total = median_run_metric(webgl2, "gpu_total_ms", "median")
    gpu_total = median_run_metric(webgpu, "gpu_total_ms", "median")
    lines += ["", "## Group median ratios", ""]
    lines.append(
        "Active CPU ratio (WebGL2 ÷ WebGPU): **" +
        (fmt(gl_active / gpu_active, 3) if gl_active is not None and gpu_active else "unavailable") +
        "**. GPU total ratio (WebGL2 ÷ WebGPU): **" +
        (fmt(gl_total / gpu_total, 3) if gl_total is not None and gpu_total else "unavailable") + "**."
    )
    lines += [
        "",
        "GPU timers are asynchronous and may omit a short unresolved tail. Compare `gpu_total_ms` only. "
        "WebGPU's current timer records total GPU render-scope duration; its raw `upload_ms` field is "
        "a placeholder, and timestamp timing excludes preceding `queue.writeBuffer` transfers.",
        "",
        "`draw_calls` is the renderer's counted draw submissions, not a count of every WebGL/WebGPU API call. "
        "Any `webgpu_stream_*` counters describe the stream subsystem only. Optional browser environment "
        "sidecars record browser version, viewport and device pixel ratio; GPU vendor/renderer/version and "
        "fallback status come from telemetry metadata.",
        "",
        "## Runs",
        "",
        "| Renderer | Source | GPU frames | Browser / GPU adapter | Fallback |",
        "| --- | --- | ---: | --- | --- |",
    ]
    for label, runs in groups:
        for run in runs:
            metadata = run["metadata"]
            env = run["environment"] or {}
            browser = env.get("browser_user_agent", "")
            adapter = metadata.get("GL_renderer", "")
            fallback = metadata.get("renderer_fallback", "none")
            lines.append(
                f"| {label} | `{Path(run['source']).name}` | {run['gpu_frames']}/{run['measured_frames']} | "
                f"{browser} / {adapter} | {fallback} |"
            )
    stream_names = sorted({
        name for run in all_runs for name in run["metrics"]["counters"] if name.startswith("webgpu_stream_")
    })
    if stream_names:
        lines += ["", "## WebGPU stream counters", "", "Median counter values per measured frame:", "",
                  "| Counter | WebGL2 | WebGPU |", "| --- | ---: | ---: |"]
        for name in stream_names:
            values = []
            for runs in (webgl2, webgpu):
                samples = [run["metrics"]["counters"].get(name) for run in runs]
                per_run = [sample["median"] for sample in samples if sample]
                values.append(statistics.median(per_run) if per_run else None)
            lines.append(f"| {name} | {fmt(values[0], 1)} | {fmt(values[1], 1)} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--webgl2", nargs="+", type=Path, required=True, help="One or more WebGL2 telemetry JSONL files")
    parser.add_argument("--webgpu", nargs="+", type=Path, required=True, help="One or more WebGPU telemetry JSONL files")
    parser.add_argument("--output", type=Path, required=True, help="Markdown report path; a JSON summary is written alongside")
    args = parser.parse_args()
    webgl2 = [read_run(path, "webgl2") for path in args.webgl2]
    webgpu = [read_run(path, "webgpu") for path in args.webgpu]
    markdown = report(webgl2, webgpu)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(markdown)
    args.output.with_suffix(".json").write_text(
        json.dumps({"webgl2": webgl2, "webgpu": webgpu}, indent=2) + "\n"
    )
    print(markdown, end="")


if __name__ == "__main__":
    main()
