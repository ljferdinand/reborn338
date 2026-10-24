# Research record

This directory is the working record of how Reborn was measured and fitted: the
per-component findings, the plans that drove each round of measurement, status
notes written at the end of working sessions, review reports, and the audit of
which findings were proven and which were retired. It is kept because the numbers
in the code came from here, and anyone continuing a component should read its
findings file before changing a constant.

It was written as a lab notebook, not as documentation, so read it with these in
mind:

- **Paths and files outside this repository.** The notes refer to the author's
  private workspace: folders of generated fixtures (`fixtures-<date>/`, each with a
  `MANIFEST.md`), ReBirth exports (`renders/`, `rb338-exports/`), the manual as page
  images (`manualPNGs/`), extracted reference samples, and a few planning files. None
  of these are published, because most of them are ReBirth's own audio or are
  derived from it. The fixture generators that made the fixtures are in
  `tools/rbsgen/`, so the fixtures themselves can be regenerated; the exports have to
  come from your own copy of ReBirth.
- **Files and tools that were not published.** The notes name several working files
  that are not in this repository. They are left as written, because they record
  what was done; this is what each one was and where its lasting content went.

  | Named in the notes | What it was | Now |
  |---|---|---|
  | `CLAUDE.md`, `reborn/CLAUDE.md`, `AGENTS.md` | instruction files for the coding agent that did much of the work, including the list of non-obvious behaviour | `docs/NON_OBVIOUS_BEHAVIOUR.md`, `docs/MEASUREMENT_PROTOCOL.md` |
  | the `ab-parity` skill | the A/B measurement protocol, in agent-instruction form | `docs/MEASUREMENT_PROTOCOL.md` |
  | the `component-deep-dive` skill | the one-component-at-a-time working method | `METHOD.md` |
  | the `rebirth-manual` skill | an index of the ReBirth manual's pages and the facts read from them | not published, since the manual is ReBirth's; the facts that mattered are in the findings and `docs/NON_OBVIOUS_BEHAVIOUR.md` |
  | `tools/rbdrive/` (`rbdrive.py`, `rbclick.py`), `docs/REBIRTH_AUTOMATION.md` | Windows automation that drove the author's own copy of ReBirth to load a fixture and export its audio, and its manual | not published; exports have to come from your own copy |
  | `tools/extract/extract_samples.py` | extracted ReBirth's own drum samples for measurement | not published; `docs/SAMPLES.md` says how samples load |
  | `docs/TEST_STRATEGY.md` | a planning note that was never committed | `docs/MEASUREMENT_PROTOCOL.md` covers the same ground |
- **Commit hashes.** The notes cite commits from the author's private history, which
  was not published (the public repository starts from a single commit). Treat a
  hash as a date stamp.
- **"Luke", "the ear", "sign-off".** The project had one maintainer, who listened to
  every change and signed each component off. Where a note says a change waited for
  sign-off or for an ear check, that was the gate at the time.
- **"Role A", "Role B", "walled implementer".** From September 2026 the effects were
  re-derived in two roles; see `METHOD.md` and `docs/PROVENANCE.md`.
- **Retired findings stay retired.** `FINDINGS_AUDIT.md` records findings that were
  later shown to be confounded. Read it before reviving an old number as a target.
- **Dates.** Every note is dated. A later note supersedes an earlier one on the same
  question, and the top-level docs (`docs/PARITY.md`, `docs/COMPONENT_MATRIX.md`,
  `docs/STATUS.md`) are the current state.

`EXPERIMENTS.md` lists three investigations that were concluded without being merged,
with their code changes as patches in `experiments/`.
