"""Extract the first valid Honest document from mixed text.

Scans for 'type' keyword at line start or inside markdown code fences,
attempts parse from each candidate position, returns the first success.
"""

from __future__ import annotations

import re

from .parser import Parser, ParseError
from .types import Document


def extract(text: str) -> Document:
    """Extract the first valid Honest document from mixed text.

    Handles:
    - Honest documents embedded in prose
    - Markdown code fences (```pascal, ```honest, or bare ```)
    - Multiple candidate positions (tries each until one parses)

    Args:
        text: Mixed text potentially containing an Honest document.

    Returns:
        The first successfully parsed Document.

    Raises:
        ValueError: If no valid Honest document is found.
    """
    # Strategy: find candidate positions where 'type' appears at line start
    # (possibly inside code fences), try to parse from each.

    candidates = []

    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip()

        # Check for code fence
        if stripped.startswith("```"):
            # Inside a code fence — collect content until closing fence
            fence_content_lines = []
            i += 1
            while i < len(lines):
                if lines[i].lstrip().startswith("```"):
                    i += 1
                    break
                fence_content_lines.append(lines[i])
                i += 1

            fence_text = "\n".join(fence_content_lines)
            # Check if fence content starts with 'type'
            fence_stripped = fence_text.lstrip()
            if fence_stripped.startswith("type") and (
                len(fence_stripped) == 4 or fence_stripped[4] in " \t\n\r"
            ):
                candidates.append(fence_stripped)
            continue

        # Check for 'type' at line start (after whitespace)
        if stripped.startswith("type") and (
            len(stripped) == 4 or stripped[4] in " \t\n\r"
        ):
            # Collect from here to end of text
            remaining = "\n".join(lines[i:])
            candidates.append(remaining.lstrip())

        i += 1

    # Try each candidate
    for candidate in candidates:
        try:
            return Parser(candidate).parse()
        except ParseError:
            continue

    raise ValueError("No valid Honest document found")
