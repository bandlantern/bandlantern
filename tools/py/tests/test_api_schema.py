"""Engine API schema: examples validate, the generated TypeScript is current, and the
schema, docs/api/messages.md and testdata/api/messages/ name the same messages."""

import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import api_schema  # noqa: E402

MESSAGES_DIR = api_schema.REPO_ROOT / "testdata" / "api" / "messages"
MESSAGES_MD = api_schema.REPO_ROOT / "docs" / "api" / "messages.md"

ROOT = api_schema.load_schema()


def load_examples(name):
    return json.loads((MESSAGES_DIR / name).read_text(encoding="utf-8"))


def doc_section(heading_prefix):
    """Text of the messages.md "## <heading_prefix>..." section, up to the next "## "."""
    text = MESSAGES_MD.read_text(encoding="utf-8")
    start = text.index(f"\n## {heading_prefix}")
    end = text.find("\n## ", start + 1)
    return text[start:end]


def doc_message_types(heading_prefix):
    return re.findall(r"^### `(\w+)`", doc_section(heading_prefix), re.MULTILINE)


@pytest.mark.parametrize("message", load_examples("commands.json") + load_examples("events.json"),
                         ids=lambda m: m["type"])
def test_valid_examples_pass(message):
    assert api_schema.validate(message, ROOT, ROOT) == []
    assert api_schema.validate_message(message, ROOT) == []


@pytest.mark.parametrize("case", load_examples("invalid.json"), ids=lambda c: c["why"])
def test_invalid_examples_fail_for_the_stated_reason(case):
    assert api_schema.validate(case["message"], ROOT, ROOT) != []
    errors = api_schema.validate_message(case["message"], ROOT)
    assert any(case["error"] in e for e in errors), errors


def test_generated_typescript_is_current():
    expected = api_schema.generate_ts(ROOT)
    assert api_schema.TS_OUTPUT.read_text(encoding="utf-8") == expected, (
        "ui/src/api.generated.ts is out of date; run: python tools/py/api_schema.py"
    )


@pytest.mark.parametrize("direction, heading, examples", [
    ("command", "Commands", "commands.json"),
    ("event", "Events", "events.json"),
])
def test_schema_docs_and_examples_name_the_same_messages(direction, heading, examples):
    in_schema = api_schema.messages(ROOT, direction)
    assert in_schema == doc_message_types(heading)
    assert set(in_schema) == {m["type"] for m in load_examples(examples)}


def test_every_message_def_has_its_own_type_const():
    for direction in ("command", "event"):
        for name in api_schema.messages(ROOT, direction):
            schema = ROOT["$defs"][name]
            variants = schema.get("oneOf", [schema])
            assert [v["properties"]["type"]["const"] for v in variants] == [name] * len(variants)


def test_root_oneof_lists_every_message():
    names = api_schema.messages(ROOT, "command") + api_schema.messages(ROOT, "event")
    assert [r["$ref"] for r in ROOT["oneOf"]] == [f"#/$defs/{n}" for n in names]


def test_error_codes_match_messages_md():
    documented = re.findall(r"^\| `(\w+)` \|", doc_section("Events"), re.MULTILINE)
    error_table = documented[documented.index("bad_version"):]
    assert ROOT["$defs"]["ErrorCode"]["enum"] == error_table


def test_unsupported_keyword_is_rejected():
    with pytest.raises(api_schema.SchemaError, match="pattern"):
        api_schema.check_keywords({"properties": {"x": {"type": "string", "pattern": "^a"}}}, "#")
