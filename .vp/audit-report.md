# Honest CLI quality audit

**Audited revision:** `07322a5987242260f72fa953839a4652427b8cc7`  
**Date:** 2026-09-29  
**Scope:** the five C command-line tools, their shared library paths, CLI tests,
build/install interface, Python package verification, and release CI/docs.

Generated files under `build/` were excluded. The format specification and
the Python implementation were sampled where they define CLI compatibility;
this was not a fresh audit of every format rule or Python parser branch.

## Baseline evidence

- `make check`: 647/647 C assertions passed across seven suites.
- `make check SANITIZE=undefined`: 647/647 passed.
- `make check-asan`: 647/647 passed; the subprocess-heavy CLI suite took about
  two minutes on the local ARM64 development host.
- GCC 11.5 with `-Wall -Wextra -Wpedantic -Wshadow -Werror`: build and all C
  tests passed when `-Iinclude` was supplied explicitly.
- GCC `-fanalyzer`: the library and all five tools built without a diagnostic.
- Python tests: 34/34 passed with Python 3.12 and pytest 9.1.1 when run with
  `PYTHONPATH=python`.
- Both cross-implementation scripts passed all six fixtures.
- Wire formatting was idempotent for all examples and the rulebook.
- `make audit` did not complete in the original environment: `pytest` was not
  installed. A clean `pip install -e './python[dev]'` also failed because the
  configured build backend could not be imported.

These results are evidence for the exercised inputs, not proof of correctness
on the four intended OS/architecture combinations.

## Findings

### BUG-1: malformed documents can be reported as successful output

**Locations:** `tools/honest-fmt.c:73`, `tools/honest-extract.c:151`,
`src/parser.c:936`  
**Invariant:** a CLI must not return success or publish an Honest document when
the parser recorded an error.  
**Observed failure:** input `type X = Integer; var x : X = nope;` made both
tools exit 0 and emit `var x : X = { ERROR: null value };`.  
**Repair:** inspect diagnostic severity even when the parser returns a partial
document; print diagnostics to stderr and fail without stdout.  
**Falsifying test:** invoke both real binaries with the malformed document and
assert non-zero status and empty stdout.

### BUG-2: output failures are reported as success

**Locations:** `src/serialise.c:363-369`, `tools/honest-build.c:553-560`,
`tools/honest-extract.c:153-159`, `tools/honest-get.c:21-53`,
`tools/honest-parse.c:99-127`  
**Invariant:** successful exit means all promised stdout bytes were written.  
**Observed failure:** valid invocations of `honest-parse`, `honest-fmt`,
`honest-get`, `honest-build`, and `honest-extract` returned 0 with stdout
redirected to `/dev/full` (the `honest-get` probe used `origin label` from
example 01).  
**Repair:** check writes and the final stream flush; return non-zero with a
stderr diagnostic on failure.  
**Falsifying test:** on systems with `/dev/full`, redirect every successful CLI
there and require non-zero status.

### BUG-3: `honest-build` silently discards caller input

**Locations:** `tools/honest-build.c:44-67`, `tools/honest-build.c:254-285`,
`tools/honest-build.c:521-535`  
**Invariant:** every supplied assignment is either consumed exactly as a valid
field path or causes a clear failure. Documented limits reject excess input.  
**Observed failure:** building `R = record x : Integer; end;` with
`x=1 surprise=2` returned 0 and silently omitted `surprise`. Assignments after
the 256th and path components after the 32nd are also silently ignored by the
loop bounds.  
**Repair:** validate paths against the target type, reject malformed/deep
paths, reject more than 256 assignments, and reject conflicting type sources.  
**Falsifying test:** pass unknown, malformed, over-deep, excessive, and
conflicting arguments and require status 1 with empty stdout.

### BUG-4: `honest-build` does not honour the Unicode `Char` contract

**Locations:** `tools/honest-build.c:175-182`,
`tools/honest-build.c:401-419`; contract at `specs/honest-spec.md:145`  
**Invariant:** a `Char` is exactly one valid Unicode scalar and round-trips
without alteration; strings advertised as UTF-8 reject overlong encodings,
surrogates, and values above U+10FFFF.  
**Failure mode:** the builder stores only the first byte of a non-ASCII
character, so `é` is interpreted as U+00C3 and serialises as `Ã`. Its UTF-8
checker validates continuation-byte shape but accepts non-canonical and
non-scalar sequences.  
**Repair:** decode and validate exactly one Unicode scalar for `Char`, and use
strict scalar-value UTF-8 validation for all assignments.  
**Falsifying test:** build/get `é` and an emoji as `Char`, reject two-character
values, and invoke the binary with malformed UTF-8 arguments.

### BUG-5: the documented Python installation is broken

**Location:** `python/pyproject.toml:3`  
**Invariant:** `pip install python/` works in a clean supported Python
environment.  
**Observed failure:** a new Python 3.12 virtual environment failed with
`BackendUnavailable: Cannot import 'setuptools.backends._legacy'`.  
**Repair:** use setuptools' supported PEP 517 backend and test wheel/editable
installation in CI.  
**Falsifying test:** install `./python[dev]` into a fresh environment, import
`honest`, and run its tests without `PYTHONPATH` source-tree assistance.

### HARDENING-1: argument handling is ambiguous

`honest-parse` and `honest-fmt` silently select the last of multiple paths;
`honest-extract` ignores extra arguments; `honest-parse -q -v` prints verbose
stdout despite `--quiet` promising to suppress all output. Reject ambiguous
combinations and test help/usage streams and statuses.

### HARDENING-2: CLI test coverage omits two installed programs

`tests/test_tools.c` runs `honest-build`, `honest-get`, and `honest-extract`,
but no end-to-end test invokes `honest-parse` or `honest-fmt`. Its helper also
discards stderr, preventing diagnostic contract checks. Add black-box shell
tests for all five executables while retaining focused C integration tests.

### HARDENING-3: the release build interface is incomplete

There is no `make test`, `make install`, `make sanitize`, or `make analyze`
interface, no staged-install check, and no GitHub Actions workflow. Warning
policy is described as `-Werror` in `CONTRIBUTING.md` but not enforced by the
default Makefile. Separate preprocessor and compiler flags so caller-provided
optimisation flags cannot accidentally remove `-Iinclude`.

### HARDENING-4: published verification counts are stale

`README.md` and `docs/testing.md` claim 637 C assertions and 51 tool assertions;
the observed totals are 647 and 61 respectively. Counts should either be kept
current or phrased so normal test growth does not make the documentation false.

## Risk order and repair sequence

1. Reject parser errors and surface I/O failures.
2. Make builder input loss and Unicode corruption impossible.
3. Repair Python packaging and install verification.
4. Add a stable build/test/install/analysis interface.
5. Define the CLI contract and limitations precisely.
6. Add the explicit Linux/macOS x86-64/ARM64 CI matrix.

Each behavioural repair requires a test observed failing against the audited
revision, then passing with the repair. Cross-platform support remains an
unverified claim until CI runs on the published repository.

## Remediation evidence

The authorised repair work completed locally in these independently reviewable
commits:

- `bb25223` — enforce CLI input, parse, Unicode, and output contracts;
- `f0eefc3` — repair the Python PEP 517 build backend;
- `79ef5ed` — add reproducible test, sanitizer, analyzer, and install targets;
- `c503bad`, `473e1ae` — validate generated variable identifiers;
- `671e171` — publish the precise CLI contract and limitations;
- `6aa0c03` — add the pinned six-platform CI matrix and Dependabot policy.

Final local verification on Linux ARM64 with GCC 11.5 observed:

- `make audit PYTHON=/tmp/honest-vp-venv/bin/python ANALYZER_CC=gcc`: passed;
- 647/647 C assertions passed in normal and ASan/UBSan builds;
- 55/55 black-box CLI checks passed in normal and ASan/UBSan builds;
- 34/34 Python tests passed;
- both conformance levels passed all 6/6 fixtures;
- GCC `-fanalyzer` completed with warnings treated as errors;
- ShellCheck accepted all test and conformance scripts;
- staged `make install` installed and ran help for all five executables, and
  installed `libhonest.a` plus `honest.h`;
- all local Markdown links resolved.

The configured Ubuntu x86-64/ARM64 GCC/Clang and macOS Intel/ARM64 jobs have not
run because this workstream was not authorised to push. Cross-platform support
therefore remains configured but externally unverified pending a GitHub CI run.

## Second hardening audit

**Date:** 2026-09-30
**Plan:** `/home/alexturner/plans/honest-hardening-plan.md`, T1–T14

An independent review after `912365f` expanded the audit to both parsers, both
serialisers, the builder, extraction fallback, large-output pipe failure, and
cross-language rejection parity. Failing tests reproduced these defects before
repair:

- all five CLIs terminated with signal status 141 when a reader closed a pipe;
- fixed and variant record values were not fully checked against declarations;
- invalid variant definitions and tags were accepted;
- C truncated multi-scalar `Char` values, accepted invalid UTF-8, saturated an
  overflowing `LongInt`, and accepted incomplete or overflowing real values;
- Python accepted non-ASCII identifiers and out-of-range integers, leaked a
  builtin `ValueError` for malformed reals, and emitted malformed `Char` text;
- extraction stopped at malformed numeric candidates rather than finding a
  later valid document; and
- both implementations accepted an empty `type` section.

The formal real-literal grammar exposed one further issue while constructing
T14: both parsers accepted exponent-only `1e2`, and both serialisers could emit
`0` for a `Real`. Focused tests failed in C, Python, and the builder before the
repair; reals now require the grammar's decimal point and integral-valued reals
serialise with `.0`.

### Second-wave commits

- `0002075` — report closed output pipes as operational failures;
- `c807583` — enforce the shared C parser invariants for T2–T8 and T13;
- `ac07e83` — align Python lexical, numeric, and declaration validation;
- `d554021` — align Python fixed and variant record validation;
- `374d7a6` — round-trip every valid Python `Char` scalar;
- `67289ce` — verify extraction skips malformed candidates;
- `bd03cc1` — add the 224-check strict C corpus and definition-time variant
  completeness warning;
- `4a1905c` — prevent the builder from producing non-finite reals;
- `1dbb8d8` — add the shared valid/invalid boundary corpus;
- `872f8a2` — exercise every staged CLI and compile/link an installed-library
  consumer;
- `1c422bb` — enforce the specified real grammar and valid real serialisation;
- `d40a53b` — preserve invalid-UTF-8 diagnostics without leaked ownership.

The C parser changes for T2–T8 and the C half of T13 share one type-directed
value-validation path and were preserved in one commit after an interrupted
work session. This is the one deviation from the plan's finer commit sequence.

The specification distinguishes normal documents, which require data, from
declarations-only rulebooks. Both public libraries expose one parse entry point
for both forms; there is no separate rulebook parse API. The repair therefore
rejects bare/comment-only `type` while retaining inputs with at least one type
declaration and no variables. Inventing a new API would have been a format and
interface redesign outside this hardening scope.

### Final local evidence

On the Linux ARM64 development host with GCC 11.5:

- `make test PYTHON=/tmp/honest-vp-venv/bin/python`: passed with 889/889 C
  assertions, 66/66 black-box CLI checks, and 113/113 Python tests;
- original cross-implementation levels 1–3: 6/6 fixtures passed at each level;
- the new boundary corpus: 54/54 checks passed, including byte-identical wire
  output, mutual parsing, idempotence, and invalid-input rejection parity;
- `make sanitize`: 889/889 C assertions and 66/66 CLI checks passed under the
  combined AddressSanitizer and UndefinedBehaviorSanitizer build;
- `make analyze ANALYZER_CC=gcc`: completed with `-fanalyzer` and warnings as
  errors;
- `shellcheck tests/*.sh scripts/*.sh`: passed;
- `make install-check`: all five staged programs ran help and representative
  commands, and a consumer compiled and linked using only staged `honest.h` and
  `libhonest.a`;
- every local Markdown link resolved; and
- `git diff --check` passed.

The six explicitly pinned GitHub jobs remain configuration evidence, not runtime
evidence, until this unpushed commit is published and Actions reports results.
