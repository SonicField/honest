"""Extraction must recover after malformed candidates without leaking errors."""

import pytest

import honest


def test_extract_skips_malformed_numeric_candidate():
    text = """type R = Real; var bad : R = 1e+;
Some prose.
type N = Integer; var good : N = 42;
"""
    assert honest.extract(text)["good"] == 42


def test_extract_skips_malformed_fenced_candidate():
    text = """```honest
type R = Real; var bad : R = 1e+;
```
type N = Integer; var good : N = 42;
"""
    assert honest.extract(text)["good"] == 42


def test_extract_all_malformed_uses_documented_exception():
    with pytest.raises(ValueError, match="No valid Honest document found"):
        honest.extract("type R = Real; var bad : R = 1e+;")
