#!/usr/bin/env python3
"""Test whether AI models can produce valid Honest documents.

Usage:
    # Validate a pre-generated AI response
    honest-ai-test.py --response-file response.txt --type EvalResult

    # Validate all responses in a directory
    honest-ai-test.py --response-dir scripts/responses/

    # Show the prompt that would be sent (for manual testing)
    honest-ai-test.py --show-prompt --type EvalResult --typedef 'type ...'

    # Use a rulebook for type definitions
    honest-ai-test.py --response-file resp.txt --type SessionMeta \
                      --rulebook lib/nbs-standard.honest-rulebook
"""

import argparse
import sys
import os
import json

# Add parent directory to path for honest import
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

import honest


PROMPT_TEMPLATE = """You are producing data in the Honest interchange format.

Honest uses Pascal-derived syntax with these rules:
- Type declarations start with 'type' keyword
- Records use 'record ... end' with fields separated by semicolons
- Enumerations use parenthesised comma-separated values: (Value1, Value2)
- Strings use single quotes with '' for escaping: 'it''s'
- Variables start with 'var name : Type = value;'
- Field assignments use 'name : value' separated by semicolons

Given this type definition:

{type_definition}

Produce a complete Honest document containing a variable named 'result' of type {type_name} with the following values:
{instructions}

Output the complete Honest document including both the type declarations and the var section. Do not include any other text or explanation — output only the Honest document."""


def load_type_definition(args) -> str:
    """Load type definition from --typedef or --rulebook."""
    if args.typedef:
        return args.typedef
    if args.rulebook:
        with open(args.rulebook, 'r') as f:
            return f.read()
    return None


def generate_prompt(type_def: str, type_name: str, instructions: str) -> str:
    """Generate a prompt for AI to produce an Honest document."""
    return PROMPT_TEMPLATE.format(
        type_definition=type_def,
        type_name=type_name,
        instructions=instructions,
    )


def validate_response(response_text: str, type_name: str = None) -> dict:
    """Validate an AI response containing an Honest document.

    Returns a dict with:
        success: bool
        document: Document or None
        error: str or None
        extracted_text: str or None
    """
    result = {
        'success': False,
        'document': None,
        'error': None,
        'extracted_text': None,
    }

    # Try to extract an Honest document
    try:
        doc = honest.extract(response_text)
    except ValueError as e:
        result['error'] = f"No valid Honest document found: {e}"
        return result

    # Serialise what we extracted for reporting
    result['extracted_text'] = honest.serialise(doc)

    # Validate structure
    if type_name and not doc.get_type(type_name):
        result['error'] = f"Type '{type_name}' not found in document"
        return result

    if len(doc.vars) == 0:
        result['error'] = "No variables in document (missing var section)"
        return result

    # Try round-trip to verify serialisation consistency
    try:
        wire = honest.serialise(doc, wire=True)
        doc2 = honest.parse(wire)
        if len(doc2.vars) != len(doc.vars):
            result['error'] = "Round-trip produced different variable count"
            return result
    except honest.ParseError as e:
        result['error'] = f"Round-trip failed: {e}"
        return result

    result['success'] = True
    result['document'] = doc
    return result


def validate_response_file(path: str, type_name: str = None) -> dict:
    """Validate an AI response from a file."""
    with open(path, 'r') as f:
        text = f.read()
    result = validate_response(text, type_name)
    result['file'] = path
    return result


def main():
    parser = argparse.ArgumentParser(description='Test AI Honest document production')
    parser.add_argument('--response-file', help='Validate a single response file')
    parser.add_argument('--response-dir', help='Validate all .txt files in directory')
    parser.add_argument('--type', help='Expected type name', default=None)
    parser.add_argument('--typedef', help='Inline type definition')
    parser.add_argument('--rulebook', help='Rulebook file for type definitions')
    parser.add_argument('--show-prompt', action='store_true',
                        help='Print the prompt template instead of validating')
    parser.add_argument('--instructions', default='Use representative example values.',
                        help='Instructions for value generation')
    args = parser.parse_args()

    if args.show_prompt:
        type_def = load_type_definition(args)
        if not type_def:
            print("Error: --typedef or --rulebook required with --show-prompt",
                  file=sys.stderr)
            return 1
        print(generate_prompt(type_def, args.type or 'Result', args.instructions))
        return 0

    results = []

    if args.response_file:
        results.append(validate_response_file(args.response_file, args.type))
    elif args.response_dir:
        for name in sorted(os.listdir(args.response_dir)):
            if name.endswith('.txt'):
                path = os.path.join(args.response_dir, name)
                results.append(validate_response_file(path, args.type))
    else:
        # Read from stdin
        text = sys.stdin.read()
        results.append(validate_response(text, args.type))

    # Report
    passed = sum(1 for r in results if r['success'])
    total = len(results)

    for r in results:
        status = 'PASS' if r['success'] else 'FAIL'
        file_info = f" [{r.get('file', 'stdin')}]" if 'file' in r else ''
        print(f"{status}{file_info}")
        if r['error']:
            print(f"  Error: {r['error']}")
        if r['extracted_text'] and not r['success']:
            print(f"  Extracted:\n    {r['extracted_text'][:200]}")

    print(f"\n{passed}/{total} passed")
    return 0 if passed == total else 1


if __name__ == '__main__':
    sys.exit(main())
