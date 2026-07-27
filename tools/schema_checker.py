#!/usr/bin/env python3
"""SynthGen Core v2 Schema Checker v1.0

Three-way diff: Code Interface Registration <-> EvidencePackage Schema <-> Theory Framework Commitments.

Checks:
  C1: Field name spelling - code registration vs schema definition (HIGH)
  C2: Required field existence - schema definition vs actual fill (HIGH)
  C3: Enum value match - applicability marking vs actual value (MEDIUM)
  C4: Applicability marking missing - 'always' fields must have fill logic (HIGH)
"""

import json
import sys
from typing import List, Dict, Any, Tuple


# Canonical EvidencePackage v1 schema definition
EVIDENCE_PACKAGE_V1_SCHEMA = {
    "fields": {
        "schema_hash": {"required": True, "applicability": "always", "type": "string"},
        "data_source": {"required": True, "applicability": "always", "type": "string"},
        "rows_generated": {"required": True, "applicability": "always", "type": "integer"},
        "rows_failed": {"required": True, "applicability": "always", "type": "integer"},
        "seed_used": {"required": True, "applicability": "always", "type": "integer"},
        "distribution": {"required": True, "applicability": "always", "type": "string"},
        "generation_timestamp": {"required": True, "applicability": "always", "type": "string"},
        "constraint_summary": {"required": True, "applicability": "always", "type": "object"},
        "tail_report": {"required": True, "applicability": "not_applicable_v1", "type": "object"},
        "statistical_fidelity": {"required": True, "applicability": "not_applicable_v1", "type": "object"},
        "audit_immutability": {"required": True, "applicability": "not_applicable_v1", "type": "object"},
        "drift_detection": {"required": True, "applicability": "not_applicable_v1", "type": "object"},
    }
}

# Canonical EvidencePackage v2 schema definition
EVIDENCE_PACKAGE_V2_SCHEMA = {
    "fields": {
        "schema_hash": {"required": True, "applicability": "always", "type": "string"},
        "data_source": {"required": True, "applicability": "always", "type": "string"},
        "rows_generated": {"required": True, "applicability": "always", "type": "integer"},
        "rows_failed": {"required": True, "applicability": "always", "type": "integer"},
        "seed_used": {"required": True, "applicability": "always", "type": "integer"},
        "distribution": {"required": True, "applicability": "always", "type": "string"},
        "generation_timestamp": {"required": True, "applicability": "always", "type": "string"},
        "constraint_summary": {"required": True, "applicability": "always", "type": "object"},
        "tail_report": {"required": True, "applicability": "conditional", "type": "object"},
        "statistical_fidelity": {"required": True, "applicability": "always", "type": "object"},
        "audit_immutability": {"required": True, "applicability": "always", "type": "object"},
        "drift_detection": {"required": True, "applicability": "conditional", "type": "object"},
        "constraint_type_breakdown": {"required": True, "applicability": "always", "type": "object"},
        "exclusion_rate": {"required": True, "applicability": "always", "type": "number"},
        "data_grade": {"required": True, "applicability": "always", "type": "string"},
        "bias_declaration": {"required": True, "applicability": "always", "type": "string"},
        "post_filter_info": {"required": False, "applicability": "conditional", "type": "object"},
        "data_engine_info": {"required": False, "applicability": "conditional", "type": "object"},
        "provenance": {"required": True, "applicability": "always", "type": "object"},
    }
}


def load_interface_registry(path: str) -> Dict[str, Any]:
    """Load a component interface registration JSON file."""
    with open(path) as f:
        return json.load(f)


def load_evidence_package(path: str) -> Dict[str, Any]:
    """Load an EvidencePackage JSON file."""
    with open(path) as f:
        return json.load(f)


def check_field_spelling(registry_fields: set, schema_fields: set) -> List[Dict[str, Any]]:
    """C1: Check field name spelling between code registration and schema definition."""
    issues = []

    # Fields in registry but not in schema (typos or removed)
    extra = registry_fields - schema_fields
    for field in sorted(extra):
        issues.append({
            "check": "C1_field_spelling",
            "severity": "HIGH",
            "field": field,
            "message": f"Field '{field}' in code registration but not in schema definition (possible typo or removed field)",
        })

    # Fields in schema but not in registry (missing implementation)
    missing = schema_fields - registry_fields
    for field in sorted(missing):
        issues.append({
            "check": "C1_field_spelling",
            "severity": "HIGH",
            "field": field,
            "message": f"Field '{field}' in schema definition but not in code registration (missing implementation)",
        })

    return issues


def check_required_fields(evidence_pkg: Dict[str, Any], schema: Dict[str, Any]) -> List[Dict[str, Any]]:
    """C2: Check required field existence in actual EvidencePackage."""
    issues = []
    fields_spec = schema.get("fields", {})

    for name, spec in fields_spec.items():
        if spec.get("required", False):
            if name not in evidence_pkg:
                issues.append({
                    "check": "C2_required_field",
                    "severity": "HIGH",
                    "field": name,
                    "message": f"Required field '{name}' missing from EvidencePackage",
                })
            elif evidence_pkg[name] is None or evidence_pkg[name] == "":
                issues.append({
                    "check": "C2_required_field",
                    "severity": "HIGH",
                    "field": name,
                    "message": f"Required field '{name}' present but empty/null",
                })

    return issues


def check_applicability(evidence_pkg: Dict[str, Any], schema: Dict[str, Any]) -> List[Dict[str, Any]]:
    """C3+C4: Check applicability marking consistency."""
    issues = []
    fields_spec = schema.get("fields", {})

    for name, spec in fields_spec.items():
        applicability = spec.get("applicability", "always")

        if applicability == "always":
            # C4: 'always' fields must have fill logic
            if name not in evidence_pkg:
                issues.append({
                    "check": "C4_applicability_missing",
                    "severity": "HIGH",
                    "field": name,
                    "message": f"Field '{name}' marked 'always' but not filled in EvidencePackage",
                })

        elif applicability.startswith("not_applicable"):
            # Fields marked not_applicable should have correct marking
            if name in evidence_pkg:
                value = evidence_pkg[name]
                if isinstance(value, dict):
                    marker = value.get("status", "")
                    if not marker.startswith("not_applicable"):
                        issues.append({
                            "check": "C3_applicability_mismatch",
                            "severity": "MEDIUM",
                            "field": name,
                            "message": f"Field '{name}' marked '{applicability}' in schema but status is '{marker}'",
                        })

    return issues


def check_enum_values(evidence_pkg: Dict[str, Any]) -> List[Dict[str, Any]]:
    """C3: Check enum value consistency for data_grade and exclusion_rate."""
    issues = []

    valid_grades = {"A", "B", "C", "D", "F"}
    data_grade = evidence_pkg.get("data_grade", "")
    if data_grade and data_grade not in valid_grades:
        issues.append({
            "check": "C3_enum_value",
            "severity": "MEDIUM",
            "field": "data_grade",
            "message": f"data_grade '{data_grade}' not in valid set {valid_grades}",
        })

    exclusion_rate = evidence_pkg.get("exclusion_rate")
    if exclusion_rate is not None:
        try:
            rate = float(exclusion_rate)
            if rate < 0.0 or rate > 1.0:
                issues.append({
                    "check": "C3_enum_value",
                    "severity": "MEDIUM",
                    "field": "exclusion_rate",
                    "message": f"exclusion_rate {rate} out of range [0.0, 1.0]",
                })
        except (ValueError, TypeError):
            issues.append({
                "check": "C3_enum_value",
                "severity": "MEDIUM",
                "field": "exclusion_rate",
                "message": f"exclusion_rate '{exclusion_rate}' is not a valid number",
            })

    return issues


def run_schema_check(
    registry_path: str = None,
    evidence_path: str = None,
    schema_version: str = "v1",
) -> List[Dict[str, Any]]:
    """Run all schema checks. Returns list of issues."""
    schema = EVIDENCE_PACKAGE_V1_SCHEMA if schema_version == "v1" else EVIDENCE_PACKAGE_V2_SCHEMA
    all_issues = []

    # C1: Field spelling check (registry vs schema)
    if registry_path:
        try:
            registry = load_interface_registry(registry_path)
            registry_fields = set()
            for component in registry.get("components", []):
                for field in component.get("evidence_fields", []):
                    if isinstance(field, str):
                        registry_fields.add(field)
                    elif isinstance(field, dict):
                        registry_fields.add(field.get("name", ""))

            schema_fields = set(schema["fields"].keys())
            all_issues.extend(check_field_spelling(registry_fields, schema_fields))
        except (FileNotFoundError, json.JSONDecodeError) as e:
            all_issues.append({
                "check": "C1_field_spelling",
                "severity": "HIGH",
                "field": "N/A",
                "message": f"Cannot load registry: {e}",
            })

    # C2+C3+C4: EvidencePackage checks
    if evidence_path:
        try:
            evidence_pkg = load_evidence_package(evidence_path)
            all_issues.extend(check_required_fields(evidence_pkg, schema))
            all_issues.extend(check_applicability(evidence_pkg, schema))
            all_issues.extend(check_enum_values(evidence_pkg))
        except (FileNotFoundError, json.JSONDecodeError) as e:
            all_issues.append({
                "check": "C2_required_field",
                "severity": "HIGH",
                "field": "N/A",
                "message": f"Cannot load evidence package: {e}",
            })

    return all_issues


def format_report(issues: List[Dict[str, Any]]) -> str:
    """Format issues as a human-readable report."""
    if not issues:
        return "Schema check passed - no issues found."

    lines = []
    high_count = sum(1 for i in issues if i["severity"] == "HIGH")
    medium_count = sum(1 for i in issues if i["severity"] == "MEDIUM")

    lines.append(f"Schema check found {len(issues)} issue(s): {high_count} HIGH, {medium_count} MEDIUM")
    lines.append("")

    for issue in issues:
        icon = "🔴" if issue["severity"] == "HIGH" else "🟡"
        lines.append(f"{icon} [{issue['severity']}] {issue['check']}: {issue['field']}")
        lines.append(f"   {issue['message']}")

    return "\n".join(lines)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="SynthGen Schema Checker v1.0")
    parser.add_argument("--registry", help="Path to component interface registration JSON")
    parser.add_argument("--evidence", help="Path to EvidencePackage JSON")
    parser.add_argument("--version", choices=["v1", "v2"], default="v1", help="Schema version")
    parser.add_argument("--json", action="store_true", help="Output JSON report")
    args = parser.parse_args()

    if not args.registry and not args.evidence:
        print("Error: at least one of --registry or --evidence is required", file=sys.stderr)
        parser.print_help()
        sys.exit(1)

    issues = run_schema_check(
        registry_path=args.registry,
        evidence_path=args.evidence,
        schema_version=args.version,
    )

    if args.json:
        report = json.dumps({"issues": issues, "total": len(issues)}, indent=2)
        print(report)
    else:
        report = format_report(issues)
        print(report)

    # Exit code: 1 if any HIGH issues
    if any(i["severity"] == "HIGH" for i in issues):
        sys.exit(1)


if __name__ == "__main__":
    main()
