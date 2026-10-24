# Security

Reborn reads files that may come from anywhere: `.rbs` songs, `.rbsx` sessions,
`.rbm` mods (including the AIFF recordings inside them), and WAV recordings, in
the command-line tools and in the plugin inside a DAW. The parsers check bounds
and sizes and cap how deeply containers may nest. Three out-of-bounds reads
found by an external review in August 2026, and a stack overflow from deeply
nested files found by the project's own review in September 2026, were fixed
with regression tests (`docs/BACKLOG.md`, entry 9; `docs/research/REVIEW_2026-09.md`,
item S1).
The code has had no further audit, and no fuzzer or AddressSanitizer job runs in
CI.

To report a vulnerability, use GitHub's private vulnerability reporting on this
repository ("Report a vulnerability" under the Security tab) rather than a public
issue. Active development has stopped, so no response time can be promised; a
report with a reproducing file and, if possible, a fix is the most useful kind.
