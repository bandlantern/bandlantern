"""Engine API schema tool: validate JSON messages, generate TypeScript types.

Source of truth: docs/api/schema/api.schema.json (docs/api/README.md, "Schema").
Writes ui/src/api.generated.ts; CI fails when the committed file differs (pytest).

Only a small JSON Schema 2020-12 subset is supported, so the generator never silently
drops a rule. Any other keyword raises SchemaError:
  validation: $ref (#/$defs/... only), type, const, enum, properties, required,
              minimum, maximum, oneOf, items, additionalProperties (maps only: an
              object schema without properties, every value checked against it)
  annotation: $schema, $id, $defs, title, description, x-api-version, x-message

Stricter than JSON Schema, to match the engine (nlohmann is_number_integer): an
"integer" or an integer const/enum value accepts only JSON integers, not 2048.0.

Usage: .venv/bin/python tools/py/api_schema.py          write the TypeScript file
       .venv/bin/python tools/py/api_schema.py --check  exit 1 if it is out of date
"""

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "docs" / "api" / "schema" / "api.schema.json"
TS_OUTPUT = REPO_ROOT / "ui" / "src" / "api.generated.ts"

VALIDATION_KEYWORDS = {
    "$ref", "type", "const", "enum", "properties", "required", "minimum", "maximum", "oneOf", "items",
    "additionalProperties",
}
ANNOTATION_KEYWORDS = {
    "$schema", "$id", "$defs", "title", "description", "x-api-version", "x-message",
}
TS_PRIMITIVES = {
    "string": "string",
    "integer": "number",
    "number": "number",
    "boolean": "boolean",
    "null": "null",
}


class SchemaError(Exception):
    pass


def load_schema(path=SCHEMA_PATH):
    schema = json.loads(Path(path).read_text(encoding="utf-8"))
    check_keywords(schema, "#")
    return schema


def check_keywords(schema, where):
    unknown = set(schema) - VALIDATION_KEYWORDS - ANNOTATION_KEYWORDS
    if unknown:
        raise SchemaError(f"{where}: unsupported keyword(s) {sorted(unknown)}")
    ref = schema.get("$ref")
    if ref is not None and not ref.startswith("#/$defs/"):
        raise SchemaError(f"{where}: only #/$defs/ refs are supported, got {ref}")
    for name, sub in schema.get("$defs", {}).items():
        check_keywords(sub, f"{where}/$defs/{name}")
    for name, sub in schema.get("properties", {}).items():
        check_keywords(sub, f"{where}/properties/{name}")
    for i, sub in enumerate(schema.get("oneOf", [])):
        check_keywords(sub, f"{where}/oneOf/{i}")
    if "items" in schema:
        check_keywords(schema["items"], f"{where}/items")
    if "additionalProperties" in schema:
        if "properties" in schema:
            raise SchemaError(f"{where}: additionalProperties only on maps (no properties)")
        check_keywords(schema["additionalProperties"], f"{where}/additionalProperties")


def resolve(root, ref):
    return root["$defs"][ref.removeprefix("#/$defs/")]


def messages(root, direction):
    """Names of the $defs marked x-message: direction ("command" or "event"), in file order."""
    return [name for name, d in root["$defs"].items() if d.get("x-message") == direction]


# --- validation -----------------------------------------------------------------------

def json_kind(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    return "object"


def matches_type(value, type_name):
    kind = json_kind(value)
    if type_name == "number":
        return kind in ("integer", "number")
    return kind == type_name


def same_value(a, b):
    return json_kind(a) == json_kind(b) and a == b


def validate(value, schema, root, path="$"):
    """Returns a list of error strings; empty means valid."""
    if "$ref" in schema:
        errors = validate(value, resolve(root, schema["$ref"]), root, path)
        if errors:
            return errors

    if "type" in schema:
        types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
        if not any(matches_type(value, t) for t in types):
            return [f"{path}: expected {' or '.join(types)}, got {json_kind(value)}"]

    if "const" in schema and not same_value(value, schema["const"]):
        return [f"{path}: expected {json.dumps(schema['const'])}"]

    if "enum" in schema and not any(same_value(value, e) for e in schema["enum"]):
        return [f"{path}: not one of {json.dumps(schema['enum'])}"]

    errors = []
    if json_kind(value) in ("integer", "number"):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{path}: below minimum {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{path}: above maximum {schema['maximum']}")

    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{path}: missing {name}")
        for name, sub in schema.get("properties", {}).items():
            if name in value:
                errors += validate(value[name], sub, root, f"{path}.{name}")

    if isinstance(value, dict) and "additionalProperties" in schema:
        for name, item in value.items():
            errors += validate(item, schema["additionalProperties"], root, f"{path}.{name}")

    if isinstance(value, list) and "items" in schema:
        for i, item in enumerate(value):
            errors += validate(item, schema["items"], root, f"{path}[{i}]")

    if "oneOf" in schema:
        passing = [s for s in schema["oneOf"] if not validate(value, s, root, path)]
        if len(passing) != 1:
            errors.append(f"{path}: matches {len(passing)} of oneOf, expected exactly 1")

    return errors


def validate_message(message, root):
    """Like validate() against the root, but checks only the $defs entry named by
    message["type"], so errors name the bad field instead of "0 of oneOf"."""
    name = message.get("type") if isinstance(message, dict) else None
    if name not in messages(root, "command") + messages(root, "event"):
        return [f"$.type: unknown message type {name!r}"]
    return validate(message, root["$defs"][name], root)


# --- TypeScript generation ------------------------------------------------------------

def ts_doc(text, indent):
    return f"{indent}/** {text} */\n" if text else ""


def ts_type(schema, root, indent):
    if "$ref" in schema:
        return resolve(root, schema["$ref"])["title"]
    if "const" in schema:
        return json.dumps(schema["const"])
    if "enum" in schema:
        return " | ".join(json.dumps(e) for e in schema["enum"])
    if "oneOf" in schema:
        return " | ".join(ts_type(s, root, indent) for s in schema["oneOf"])
    if "properties" in schema:
        return "{\n" + ts_members(schema, root, indent + "  ") + indent + "}"
    if "items" in schema:
        return f"Array<{ts_type(schema['items'], root, indent)}>"
    if "additionalProperties" in schema:
        return f"{{ [name: string]: {ts_type(schema['additionalProperties'], root, indent)} }}"
    types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
    return " | ".join(TS_PRIMITIVES[t] for t in types)


def ts_members(schema, root, indent):
    required = set(schema.get("required", []))
    out = ""
    for name, sub in schema["properties"].items():
        optional = "" if name in required else "?"
        out += ts_doc(sub.get("description"), indent)
        out += f"{indent}{name}{optional}: {ts_type(sub, root, indent)};\n"
    return out


def ts_definition(schema, root):
    title = schema["title"]
    doc = ts_doc(schema.get("description"), "")
    if "properties" in schema:
        return f"{doc}export interface {title} {{\n{ts_members(schema, root, '  ')}}}\n"
    if "oneOf" in schema:
        variants = "".join(f"\n  | {ts_type(s, root, '  ')}" for s in schema["oneOf"])
        return f"{doc}export type {title} ={variants};\n"
    return f"{doc}export type {title} = {ts_type(schema, root, '')};\n"


def generate_ts(root):
    rel_schema = SCHEMA_PATH.relative_to(REPO_ROOT).as_posix()
    parts = [
        f"// Generated by tools/py/api_schema.py from {rel_schema}. Do not edit;\n"
        "// change the schema and run: python tools/py/api_schema.py\n"
        f"// Engine API {root['x-api-version']}. Rules and meaning: docs/api/messages.md.\n"
    ]
    for name, schema in root["$defs"].items():
        if "title" not in schema:
            raise SchemaError(f"#/$defs/{name}: needs a title (the TypeScript name)")
        parts.append(ts_definition(schema, root))
    for direction, union in (("command", "EngineCommand"), ("event", "EngineEvent")):
        titles = [root["$defs"][n]["title"] for n in messages(root, direction)]
        parts.append(f"export type {union} =" + "".join(f"\n  | {t}" for t in titles) + ";\n")
    return "\n".join(parts)


def main():
    text = generate_ts(load_schema())
    if "--check" in sys.argv[1:]:
        if TS_OUTPUT.read_text(encoding="utf-8") != text:
            print(f"{TS_OUTPUT} is out of date; run: python tools/py/api_schema.py")
            sys.exit(1)
        return
    TS_OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {TS_OUTPUT}")


if __name__ == "__main__":
    main()
