#!/usr/bin/env python3
"""Deterministic-optimiser driver for the Reborn 303 parameter fit.

Minimises f(params) = distance(render(params), reference) with no LLM in the
inner loop: pure CPU, reproducible, and it will not invent a reason a move
helped. Methods:

  grid        coarse sweep over a lattice (numpy only)
  neldermead  local downhill polish from a start point (scipy)
  cma         CMA-ES for rugged, coupled landscapes (the `cma` package)
  auto        grid to find the neighbourhood, then Nelder-Mead to polish
              (the recommended Component A workflow)

Component B note: do NOT add the coupled diode-ladder parameters to the search
space until the discretisation is derived from D'Angelo-Fontana and
D'Angelo-Valimaki. An optimiser fits the constants of a structure; it cannot fix
a wrong topology, and pointing CMA-ES at the unresolved ladder will return a
confident set of numbers that sound wrong. Get the topology right, then let
CMA-ES fit it.
"""
from __future__ import annotations

import argparse
import datetime as dt
import itertools
import json
import shutil
from pathlib import Path
from typing import Any, Callable

import numpy as np

Evaluate = Callable[[dict[str, float]], dict]


def _now() -> str:
    return dt.datetime.now().isoformat(timespec="seconds")


def ordered_params(search_space: dict[str, Any]) -> list[str]:
    """Stable parameter order (sorted by name) for vector packing."""
    return sorted(search_space)


def vec_to_params(x, names: list[str]) -> dict[str, float]:
    return {n: float(v) for n, v in zip(names, x)}


def bounds_for(search_space: dict[str, Any], names: list[str]):
    lows = np.array([search_space[n]["low"] for n in names], dtype=float)
    highs = np.array([search_space[n]["high"] for n in names], dtype=float)
    return lows, highs


class Runner:
    """Wraps an evaluate() callable with trial logging, bound-clipping, and
    best-tracking. The optimiser backends call `runner.f(x)`."""

    def __init__(self, search_space, evaluate: Evaluate, workdir: Path,
                 on_new_best: Callable[[dict], None] | None = None,
                 max_evals: int | None = None):
        self.names = ordered_params(search_space)
        self.lows, self.highs = bounds_for(search_space, self.names)
        self.evaluate = evaluate
        self.workdir = workdir
        self.on_new_best = on_new_best
        self.max_evals = max_evals
        self.trials = 0
        self.best: dict | None = None
        self.log_path = workdir / "trials.jsonl"
        workdir.mkdir(parents=True, exist_ok=True)

    def seed_best(self, best: dict) -> None:
        self.best = best

    def f(self, x) -> float:
        if self.max_evals is not None and self.trials >= self.max_evals:
            # Return the best-known value so scipy/cma converge and stop.
            return self.best["objective"] if self.best else float("inf")
        x = np.clip(np.asarray(x, dtype=float), self.lows, self.highs)
        params = vec_to_params(x, self.names)
        result = self.evaluate(params)
        self.trials += 1
        value = float(result["objective"])

        record = {
            "t": _now(), "trial": self.trials, "params": params,
            "objective": value, "delta_db": result.get("delta_db"),
        }
        with self.log_path.open("a") as fh:
            fh.write(json.dumps(record) + "\n")

        if self.best is None or value < self.best["objective"]:
            self.best = {"objective": value, "params": params, "trial": self.trials,
                         "delta_db": result.get("delta_db"), "updated": _now()}
            if self.on_new_best:
                self.on_new_best(self.best)
        return value


def run_grid(runner: Runner, search_space, names) -> None:
    axes = []
    for n in names:
        spec = search_space[n]
        axes.append(np.linspace(spec["low"], spec["high"], int(spec.get("grid", 5))))
    total = int(np.prod([len(a) for a in axes]))
    if total > 5000:
        raise SystemExit(f"grid has {total} points; lower the per-axis `grid` counts or use auto/cma.")
    for combo in itertools.product(*axes):
        runner.f(np.array(combo))


def run_neldermead(runner: Runner, search_space, names, x0, max_evals) -> None:
    from scipy.optimize import minimize
    bounds = list(zip(runner.lows, runner.highs))
    minimize(runner.f, np.asarray(x0, dtype=float), method="Nelder-Mead",
             bounds=bounds, options={"maxfev": max_evals, "xatol": 1e-4, "fatol": 1e-4})


def run_cma(runner: Runner, search_space, names, x0, max_evals) -> None:
    try:
        import cma
    except ImportError:
        raise SystemExit("CMA-ES needs the `cma` package: pip install cma")
    sigma0 = float(np.mean(runner.highs - runner.lows) * 0.25)
    es = cma.CMAEvolutionStrategy(
        list(x0), sigma0,
        {"bounds": [list(runner.lows), list(runner.highs)], "maxfevals": max_evals, "verbose": -9},
    )
    es.optimize(runner.f)


def run(search_space, evaluate: Evaluate, method: str, workdir: Path,
        max_evals: int, on_new_best=None, resume_best: dict | None = None) -> dict:
    names = ordered_params(search_space)
    runner = Runner(search_space, evaluate, workdir, on_new_best, max_evals)
    if resume_best:
        runner.seed_best(resume_best)

    x0 = [resume_best["params"][n] if resume_best else search_space[n].get("init",
          (search_space[n]["low"] + search_space[n]["high"]) / 2) for n in names]

    if method == "grid":
        run_grid(runner, search_space, names)
    elif method == "neldermead":
        run_neldermead(runner, search_space, names, x0, max_evals)
    elif method == "cma":
        run_cma(runner, search_space, names, x0, max_evals)
    elif method == "auto":
        # Coarse grid to locate the neighbourhood, then polish from the best cell.
        coarse = {n: {**search_space[n], "grid": min(int(search_space[n].get("grid", 3)), 4)}
                  for n in names}
        run_grid(runner, coarse, names)
        polish_x0 = [runner.best["params"][n] for n in names]
        run_neldermead(runner, search_space, names, polish_x0, max_evals)
    else:
        raise SystemExit(f"unknown method '{method}'")

    if runner.best is None:
        raise SystemExit("no successful evaluations")
    return runner.best


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", required=True, help="path to the harness config JSON")
    ap.add_argument("--method", default="auto", choices=["auto", "grid", "neldermead", "cma"])
    ap.add_argument("--max-evals", type=int, default=200, help="evaluation cap for neldermead/cma")
    ap.add_argument("--resume", action="store_true", help="seed from an existing best.json in workdir")
    args = ap.parse_args()

    config = json.loads(Path(args.config).read_text())
    workdir = Path(config.get("workdir", ".opt_runs"))
    workdir.mkdir(parents=True, exist_ok=True)
    last_wav = str(workdir / "last_candidate.wav")
    best_wav = str(workdir / "best_candidate.wav")
    best_json = workdir / "best.json"

    # Import here so the optimisation core stays importable without a renderer,
    # which is what lets the driver be unit-tested with a fake evaluate().
    from harness import Harness
    harness = Harness(config)

    def evaluate(params: dict[str, float]) -> dict:
        return harness.evaluate(params, last_wav)

    def on_new_best(best: dict) -> None:
        shutil.copyfile(last_wav, best_wav)
        best_json.write_text(json.dumps({
            **best,
            "env": {config["search_space"][k]["env"]: v for k, v in best["params"].items()},
            "candidate_wav": best_wav,
            "reference": config["reference"],
        }, indent=2))

    resume_best = None
    if args.resume and best_json.is_file():
        resume_best = json.loads(best_json.read_text())
        print(f"resuming from {best_json} (objective {resume_best['objective']:.4f})")

    best = run(config["search_space"], evaluate, args.method, workdir,
               args.max_evals, on_new_best, resume_best)

    print("\nbest:")
    print(f"  objective: {best['objective']:.5f}  (trial {best['trial']})")
    for k, v in best["params"].items():
        print(f"  {config['search_space'][k]['env']} = {v:.5f}")
    print(f"  best render: {best_wav}")
    print(f"  written:     {best_json}")
    print("\nEar-check the best render against the reference before signing off; "
          "the optimiser proposes, your ear disposes.")


if __name__ == "__main__":
    main()
