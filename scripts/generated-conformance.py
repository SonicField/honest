#!/usr/bin/env python3
"""Deterministic generated conformance checks for both implementations."""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import warnings
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

import honest  # noqa: E402


def quoted(value: str) -> str:
    value = value.replace("\\", "\\\\")
    value = value.replace("'", "''")
    value = value.replace("\n", "\\n").replace("\r", "\\r")
    return f"'{value}'"


def generated_document(index: int, rng: random.Random) -> str:
    suffix = f"G{index}"
    text = rng.choice(["plain", "it's sound", "café", "rocket 🚀", "a\\b"])
    character = rng.choice(["A", "é", "🚀", "'"])
    integer = rng.choice([-2147483648, -1, 0, 1, 2147483647])
    longint = rng.choice([-9223372036854775808, 0, 9223372036854775807])
    real = rng.choice(["0.0", "-1.5", "1.0e+10", "1.7976931348623157e308"])
    choice = rng.choice(["First", "Second", "Third"])

    if index % 3 == 0:
        return f"""{{ generated {index} }}
type
  Choice{suffix} = (First, Second, Third);
  Text{suffix} = String;
  Row{suffix} = record
    integer : Integer;
    longint : LongInt;
    real : Real;
    character : Char;
    text : Text{suffix};
    choice : Choice{suffix};
    flag : Boolean;
  end;
var value{suffix} : Row{suffix} = (
  integer : {integer};
  longint : {longint};
  real : {real};
  character : {quoted(character)};
  text : {quoted(text)};
  choice : {choice};
  flag : {'True' if index % 2 else 'False'};
);
"""

    if index % 3 == 1:
        arm_value = {
            "First": f"; number : {integer}",
            "Second": "",
            "Third": f"; text : {quoted(text)}",
        }[choice]
        return f"""type
  Choice{suffix} = (First, Second, Third);
  Tagged{suffix} = record
    common : String;
    case kind : Choice{suffix} of
      First : (number : Integer);
      Second : ();
      Third : (text : String);
  end;
var value{suffix} : Tagged{suffix} = (
  common : {quoted('common ' + text)};
  kind : {choice}{arm_value};
);
"""

    values = "; ".join(str(rng.randint(-100000, 100000)) for _ in range(index % 7))
    return f"""(* generated {index} *)
type
  Number{suffix} = Integer;
  Numbers{suffix} = sequence of Number{suffix};
  Box{suffix} = record values : Numbers{suffix}; label : String; end;
var value{suffix} : Box{suffix} = (
  values : ({values});
  label : {quoted(text)};
);
"""


def invalid_document(index: int) -> str:
    suffix = f"B{index}"
    cases = [
        f"type E{suffix} = (A, A); var x : E{suffix} = A;",
        f"type E{suffix} = (); var x : E{suffix} = A;",
        f"type A{suffix} = Integer B{suffix} = String; var x : A{suffix} = 1;",
        f"type A{suffix} = B{suffix}; B{suffix} = Integer; var x : A{suffix} = 1;",
        f"type T{suffix} = Byte; var x : T{suffix} = 256;",
        f"type T{suffix} = Integer; var x : T{suffix} = 1x;",
        f"type T{suffix} = Real; var x : T{suffix} = 1;",
        f"type R{suffix} = record x : Integer; y : String; end; var r : R{suffix} = (x : 1);",
        f"type R{suffix} = record x : Integer; y : String; end; var r : R{suffix} = (y : 'a'; x : 1);",
        f"type R{suffix} = record x : Integer; end; var r : R{suffix} = (x : 1; y : 2);",
        f"type K{suffix} = (A); R{suffix} = record case kind : K{suffix} of Z : (); end; var r : R{suffix} = (kind : A);",
        f"type K{suffix} = (A); R{suffix} = record case kind : K{suffix} of end; var r : R{suffix} = (kind : A);",
        f"type K{suffix} = (A); R{suffix} = record case kind : K{suffix} of A : (); A : (); end; var r : R{suffix} = (kind : A);",
        f"type K{suffix} = (A); R{suffix} = record case kind : K{suffix} of A : (x : Integer); end; var r : R{suffix} = (kind : A);",
        f"type K{suffix} = (A, B); R{suffix} = record case kind : K{suffix} of A : (x : Integer); B : (); end; var r : R{suffix} = (kind : B; x : 1);",
        f"type T{suffix} = Integer; var x : T{suffix} = 1; trailing",
        f"var x : Integer = 1;",
        f"type T{suffix} = Integer;",
        f"type E{suffix} = (True); var x : E{suffix} = True;",
        f"type Café{suffix} = Integer; var x : Integer = 1;",
        f"type S{suffix} = String; var x : S{suffix} = '\\uD800';",
        f"type S{suffix} = sequence of Integer; var x : S{suffix} = (1 2);",
        f"type T{suffix} = Integer; var x : T{suffix} = 1",
        f"type T{suffix} = Integer; T{suffix} = String; var x : T{suffix} = 1;",
    ]
    return cases[index % len(cases)]


def run_c(binary: Path, args: list[str], source: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [str(binary), *args, "-"], input=source.encode("utf-8"),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )


def require(condition: bool, message: str, source: str) -> None:
    if not condition:
        raise AssertionError(f"{message}\n--- source ---\n{source}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", type=int, default=64)
    parser.add_argument("--bin-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    c_parse = args.bin_dir / "honest-parse"
    c_fmt = args.bin_dir / "honest-fmt"
    if not c_parse.is_file() or not c_fmt.is_file():
        parser.error("C tools are not built")

    rng = random.Random(0x484F4E455354)
    checks = 0
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        for index in range(args.cases):
            source = generated_document(index, rng)
            py_doc = honest.parse_document(source)
            checks += 1
            c_result = run_c(c_parse, ["--quiet", "--document"], source)
            require(c_result.returncode == 0, "C rejected generated valid document", source)
            checks += 1

            py_wire = honest.serialise(py_doc, wire=True)
            c_wire_result = run_c(c_fmt, ["--wire"], source)
            require(c_wire_result.returncode == 0, "C could not serialise valid document", source)
            c_wire = c_wire_result.stdout.decode("utf-8")
            require(c_wire == py_wire, "wire serialisers diverged", source)
            checks += 1

            honest.parse_document(c_wire)
            checks += 1
            c_cross = run_c(c_parse, ["--quiet", "--document"], py_wire)
            require(c_cross.returncode == 0, "C rejected Python wire output", source)
            checks += 1

            c_again = run_c(c_fmt, ["--wire"], c_wire)
            require(c_again.returncode == 0 and c_again.stdout.decode("utf-8") == c_wire,
                    "C wire formatting is not idempotent", source)
            require(honest.serialise(honest.parse_document(py_wire), wire=True) == py_wire,
                    "Python wire formatting is not idempotent", source)
            checks += 2

            rulebook = source.split("\nvar ", 1)[0]
            honest.parse_rulebook(rulebook)
            checks += 1
            c_rulebook = run_c(c_parse, ["--quiet", "--rulebook"], rulebook)
            require(c_rulebook.returncode == 0,
                    "C rejected generated valid rulebook", rulebook)
            checks += 1

            for implementation, rejected in (
                ("Python", lambda: honest.parse_rulebook(source)),
                ("C", lambda: run_c(c_parse, ["--quiet", "--rulebook"], source)),
            ):
                try:
                    result = rejected()
                    accepted = (result.returncode == 0) if implementation == "C" else True
                except honest.ParseError:
                    accepted = False
                require(not accepted,
                        f"{implementation} accepted document as rulebook", source)
                checks += 1

            missing_final_semicolon = source.rstrip()[:-1]
            try:
                honest.parse_document(missing_final_semicolon)
            except honest.ParseError:
                pass
            else:
                require(False, "Python accepted semicolon-deletion mutation", source)
            checks += 1
            c_mutation = run_c(
                c_parse, ["--quiet", "--document"], missing_final_semicolon)
            require(c_mutation.returncode != 0,
                    "C accepted semicolon-deletion mutation", source)
            checks += 1

        for index in range(args.cases):
            source = invalid_document(index)
            try:
                honest.parse_document(source)
            except honest.ParseError:
                pass
            else:
                require(False, "Python accepted generated invalid document", source)
            checks += 1

            c_result = run_c(c_parse, ["--quiet", "--document"], source)
            require(c_result.returncode != 0, "C accepted generated invalid document", source)
            checks += 1

    print(f"{checks}/{checks} generated cross-implementation checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
