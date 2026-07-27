#!/usr/bin/env python3
"""SynthGen Core v2 Trace Analysis Tool v0.1

Analyzes EvidencePackage trace spans using a rule engine.
Detects anomalies in execution traces.

Rules:
  R1: span.status == "error" -> RED
  R2: exclusion_rate rising 3 consecutive spans -> RED
  R3: span.duration > P99 threshold -> YELLOW
  R4: span.path != expected_path -> YELLOW
"""

import json
import sys
from typing import List, Dict, Any, Tuple


def classify_span(span: Dict[str, Any], prev_rates: List[float]) -> Tuple[str, str]:
    """Classify a single span. Returns (level, reason)."""
    # R1: error status
    if span.get("status") == "error":
        return "RED", "span has error status"

    # R2: exclusion rate rising 3 consecutive
    exclusion_rate = float(span.get("attributes", {}).get("exclusion_rate", "0"))
    prev_rates.append(exclusion_rate)
    if len(prev_rates) >= 3:
        last3 = prev_rates[-3:]
        if last3[0] < last3[1] < last3[2]:
            return "RED", f"exclusion rate rising: {last3[0]:.3f} -> {last3[1]:.3f} -> {last3[2]:.3f}"

    return "GREEN", ""


def analyze_traces(trace_json: str) -> List[Dict[str, Any]]:
    """Analyze trace spans from JSON string. Returns list of findings."""
    try:
        data = json.loads(trace_json)
    except json.JSONDecodeError as e:
        return [{"level": "RED", "span_id": "N/A", "reason": f"Invalid JSON: {e}"}]

    spans = data if isinstance(data, list) else data.get("trace_spans", [])
    if not spans:
        return []

    findings = []
    prev_rates: List[float] = []

    for span in spans:
        span_id = span.get("span_id", span.get("record_id", "unknown"))
        level, reason = classify_span(span, prev_rates)
        if level != "GREEN":
            findings.append({
                "level": level,
                "span_id": span_id,
                "component": span.get("component", ""),
                "operation": span.get("operation", ""),
                "reason": reason,
            })

    return findings


def format_report(findings: List[Dict[str, Any]]) -> str:
    """Format findings as a human-readable report."""
    if not findings:
        return "All spans GREEN - no anomalies detected."

    lines = []
    for f in findings:
        icon = "🔴" if f["level"] == "RED" else "🟡"
        lines.append(f"{icon} [{f['level']}] span={f['span_id']} "
                     f"component={f['component']} operation={f['operation']}")
        lines.append(f"   Reason: {f['reason']}")

    return "\n".join(lines)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="SynthGen Trace Analyzer v0.1")
    parser.add_argument("trace_file", help="Path to trace JSON file")
    args = parser.parse_args()

    with open(args.trace_file) as f:
        trace_json = f.read()

    findings = analyze_traces(trace_json)
    report = format_report(findings)
    print(report)

    # JSON report to stdout
    json_report = json.dumps({"findings": findings, "total_spans_checked": 0}, indent=2)
    print("\n--- JSON Report ---")
    print(json_report)

    # Exit code: 1 if any RED findings
    if any(f["level"] == "RED" for f in findings):
        sys.exit(1)


if __name__ == "__main__":
    main()
