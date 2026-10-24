#!/usr/bin/env python3
"""The optimisation harness: one evaluation of f(params).

evaluate(params) does exactly three things, in order:
  1. Render. Set the REBORN_* environment variables from `params`, then run
     reborn_render with fixed, deterministic base arguments.
  2. Guard. Assert the render honoured the methodology invariants before it is
     allowed to score (the isolation/effects-off flags are present, and the
     output has the expected channel count so a mono/stereo regression cannot
     pass silently).
  3. Measure. Score the render against the reference export via metrics.objective.

The render controls the parameters through the environment because reborn_render
reads its tuning knobs from REBORN_* variables (e.g. REBORN_AMP_DECAY); the CLI
flags handle isolation (--solo bass1), effects (--no-fx), and determinism
(--seconds, --sr).
"""
from __future__ import annotations

import subprocess
import wave
from pathlib import Path
from typing import Any

import metrics


class HarnessError(RuntimeError):
    pass


class Harness:
    def __init__(self, config: dict[str, Any]):
        self.cfg = config
        self.render = config["render"]
        self.search_space = config["search_space"]
        self.reference = config["reference"]
        self.obj = config.get("objective", {})

        # Fail fast on setup problems rather than midway through a long run.
        if not Path(self.reference).is_file():
            raise HarnessError(f"reference export not found: {self.reference}")

        base = self.render.get("base_args", [])
        for flag in self.render.get("required_args", []):
            if flag not in base:
                raise HarnessError(
                    f"required render flag '{flag}' missing from base_args. This enforces "
                    f"the 303-isolated, effects-off invariant; do not remove it."
                )

    def _env_for(self, params: dict[str, float]) -> dict[str, str]:
        import os
        env = os.environ.copy()
        for k, v in self.render.get("env_fixed", {}).items():
            env[str(k)] = str(v)
        for name, value in params.items():
            spec = self.search_space[name]
            env[spec["env"]] = repr(float(value)) if isinstance(value, float) else str(value)
        return env

    def evaluate(self, params: dict[str, float], out_path: str) -> dict:
        env = self._env_for(params)
        cmd = [self.render["binary"], *self.render.get("base_args", []),
               self.render.get("out_flag", "--out"), out_path]
        try:
            proc = subprocess.run(
                cmd, env=env, capture_output=True, text=True,
                timeout=self.render.get("timeout_s", 120),
            )
        except FileNotFoundError as e:
            raise HarnessError(f"could not run renderer '{self.render['binary']}': {e}")
        except subprocess.TimeoutExpired:
            raise HarnessError(
                f"render timed out after {self.render.get('timeout_s', 120)}s for params {params}"
            )
        if proc.returncode != 0:
            raise HarnessError(f"render failed (exit {proc.returncode}): {proc.stderr.strip()}")

        # Guardrail: the output must have the expected channel count. reborn_render
        # writes stereo; a regression to mono (or a downmix mistake) would quietly
        # skew every band measurement, so refuse to score it.
        expect = int(self.render.get("expect_channels", 2))
        with wave.open(out_path, "rb") as w:
            got = w.getnchannels()
        if got != expect:
            raise HarnessError(
                f"render produced {got}-channel audio, expected {expect}. "
                f"Check the renderer and expect_channels before trusting any score."
            )

        result = metrics.objective(
            out_path, self.reference,
            kind=self.obj.get("type", "band_distance"),
            seconds=self.obj.get("seconds"),
            weights=self.obj.get("band_weights"),
        )
        result["params"] = dict(params)
        return result
