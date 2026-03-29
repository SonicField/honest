# Contributing to Honest

Thank you for your interest in the Honest interchange format.

## Project Status

Honest is a production format with a stable specification, a C reference implementation, and a Python library. The spec is versioned (v1.0). Breaking changes to the format require a new spec version.

## How to Contribute

### Reporting Issues

If something doesn't work or doesn't make sense, open an issue with:
- A clear, descriptive title
- What you were trying to do
- What happened vs what you expected
- The Honest document or code snippet that triggered the problem

### Suggesting Improvements

Feature requests are welcome. Please open an issue describing:
- The problem or gap you've encountered
- How you think it should work
- Whether the change affects the format specification, the tooling, or both

Changes to the format specification are held to a higher bar than tooling changes. The spec is a contract — consumers depend on it.

### Pull Requests

Pull requests are welcome, especially for:
- Bug fixes in the C library or CLI tools
- New language bindings (Python, Rust, Go, etc.)
- Documentation improvements
- Test coverage improvements
- Performance improvements that do not change the API

### Before Submitting

1. **Run the test suite.** `make check` must pass with zero failures. `make check-asan` must also pass.
2. **Follow the existing style.** C code uses C11, 4-space indentation, and compiles cleanly with `-Wall -Wextra -Wpedantic -Werror`.
3. **Add tests for new functionality.** Every new feature or bug fix should include a test that would have caught the issue.
4. **Keep the spec and implementation in sync.** If your change affects behaviour described in `specs/honest-spec.md`, update the spec.

### What We Won't Accept

- Changes that break backward compatibility with existing valid documents
- External dependencies in `libhonest` (it must remain dependency-free)
- Changes that privilege one consumer (human, AI, machine) over the others

## Architecture

```
honest/
  specs/          # Format specification (the contract)
  docs/           # User and implementor guides
  include/        # Public C API (honest.h)
  src/            # Library implementation
  tools/          # CLI tools (honest-parse, honest-fmt, honest-get,
                  #            honest-build, honest-extract)
  tests/          # Test suite (C)
  lib/            # Rulebook files
  examples/       # Annotated example documents
  research/       # Design rationale and surveys
```

The library (`src/`) has no dependencies beyond the C standard library. The CLI tools link against the library. Tests link against the library.

## Licence

Contributions are made under the [MIT Licence](LICENSE), the same licence as the project.
