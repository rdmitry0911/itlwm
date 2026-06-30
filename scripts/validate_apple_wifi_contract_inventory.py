#!/usr/bin/env python3
"""Validate the Apple Wi-Fi contract inventory evidence."""

import argparse
import json
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nonempty_list(value, name):
    require(isinstance(value, list) and len(value) > 0, f"{name} must be a non-empty list")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inventory_path")
    parser.add_argument("--expected-step-id", required=True)
    parser.add_argument("--expected-roadmap-item-id", required=True)
    parser.add_argument("--expected-goal-item-id", required=True)
    parser.add_argument("--expected-input-head", required=True)
    args = parser.parse_args()

    with open(args.inventory_path, "r", encoding="utf-8") as handle:
        inventory = json.load(handle)

    require(
        inventory.get("selected_step_id") == args.expected_step_id,
        "selected_step_id does not match selected step",
    )
    require(
        inventory.get("roadmap_item_id") == args.expected_roadmap_item_id,
        "roadmap_item_id does not match selected step",
    )
    require(
        args.expected_goal_item_id in inventory.get("goal_item_ids", []),
        "selected goal item is missing",
    )
    require(inventory.get("input_head") == args.expected_input_head, "input_head mismatch")
    require(inventory.get("all_gaps_linked_to_roadmap") is True, "gap linkage flag is not true")

    contracts = inventory.get("contracts")
    criteria = inventory.get("payload_state_acceptance_criteria")
    gaps = inventory.get("gap_ledger")
    nonempty_list(contracts, "contracts")
    nonempty_list(criteria, "payload_state_acceptance_criteria")
    nonempty_list(gaps, "gap_ledger")

    gap_by_id = {}
    for gap in gaps:
        gap_id = gap.get("id")
        require(gap_id and gap_id not in gap_by_id, f"duplicate or empty gap id: {gap_id}")
        require(gap.get("represented_as_roadmap_work") is True, f"{gap_id} is not roadmap work")
        require(gap.get("status") == "open", f"{gap_id} must remain open roadmap work")
        require(gap.get("roadmap_item_id", "").startswith("itlwm-rm-"), f"{gap_id} lacks roadmap item")
        require(gap.get("goal_item_id", "").startswith("itlwm-fg-"), f"{gap_id} lacks goal item")
        require(gap.get("work_type") in {"feature_layer", "validation_layer", "defect_isolation"}, f"{gap_id} has invalid work_type")
        require(gap.get("closure_artifact"), f"{gap_id} lacks closure artifact")
        nonempty_list(gap.get("acceptance_mapping"), f"{gap_id}.acceptance_mapping")
        gap_by_id[gap_id] = gap

    criteria_by_id = {}
    criteria_by_contract = {}
    for criterion in criteria:
        criterion_id = criterion.get("id")
        contract_id = criterion.get("contract_id")
        require(criterion_id and criterion_id not in criteria_by_id, f"duplicate or empty criterion id: {criterion_id}")
        require(contract_id, f"{criterion_id} lacks contract_id")
        nonempty_list(criterion.get("roadmap_item_ids"), f"{criterion_id}.roadmap_item_ids")
        nonempty_list(criterion.get("payload_acceptance"), f"{criterion_id}.payload_acceptance")
        nonempty_list(criterion.get("state_machine_acceptance"), f"{criterion_id}.state_machine_acceptance")
        criteria_by_id[criterion_id] = criterion
        criteria_by_contract.setdefault(contract_id, []).append(criterion_id)

    contract_ids = set()
    domains = set()
    for contract in contracts:
        contract_id = contract.get("id")
        require(contract_id and contract_id not in contract_ids, f"duplicate or empty contract id: {contract_id}")
        contract_ids.add(contract_id)
        domains.add(contract.get("domain"))
        require(contract.get("title"), f"{contract_id} lacks title")
        require(contract.get("domain"), f"{contract_id} lacks domain")
        nonempty_list(contract.get("driver_facing_surface"), f"{contract_id}.driver_facing_surface")
        nonempty_list(contract.get("source_paths"), f"{contract_id}.source_paths")
        require(isinstance(contract.get("payload_shape"), dict) and contract.get("payload_shape"), f"{contract_id} lacks payload_shape")
        require(isinstance(contract.get("state_machine"), dict) and contract.get("state_machine"), f"{contract_id} lacks state_machine")
        nonempty_list(contract.get("current_gap_ids"), f"{contract_id}.current_gap_ids")
        nonempty_list(contract.get("roadmap_work"), f"{contract_id}.roadmap_work")
        nonempty_list(contract.get("payload_state_acceptance_criteria_ids"), f"{contract_id}.payload_state_acceptance_criteria_ids")

        for gap_id in contract["current_gap_ids"]:
            require(gap_id in gap_by_id, f"{contract_id} references unknown gap {gap_id}")

        for work in contract["roadmap_work"]:
            gap_id = work.get("gap_id")
            require(gap_id in gap_by_id, f"{contract_id} roadmap work references unknown gap {gap_id}")
            require(
                work.get("roadmap_item_id") == gap_by_id[gap_id].get("roadmap_item_id"),
                f"{contract_id} roadmap item mismatch for {gap_id}",
            )
            require(work.get("closure_requirement"), f"{contract_id} roadmap work lacks closure requirement")

        for criterion_id in contract["payload_state_acceptance_criteria_ids"]:
            require(criterion_id in criteria_by_id, f"{contract_id} references unknown criterion {criterion_id}")
            require(
                criteria_by_id[criterion_id].get("contract_id") == contract_id,
                f"{contract_id} criterion {criterion_id} points to another contract",
            )
        require(contract_id in criteria_by_contract, f"{contract_id} lacks payload/state acceptance criteria")

    summary = inventory.get("coverage_summary", {})
    require(summary.get("contract_count") == len(contracts), "contract_count summary mismatch")
    require(
        summary.get("payload_state_acceptance_criteria_count") == len(criteria),
        "acceptance criteria summary mismatch",
    )
    require(summary.get("gap_count") == len(gaps), "gap_count summary mismatch")
    require(summary.get("roadmap_bound_gap_count") == len(gaps), "roadmap_bound_gap_count mismatch")
    require(
        summary.get("contracts_with_payload_state_acceptance") == len(contracts),
        "contracts_with_payload_state_acceptance mismatch",
    )

    print(
        json.dumps(
            {
                "status": "passed",
                "contracts": len(contracts),
                "contract_domains": len(domains),
                "roadmap_bound_gaps": len(gaps),
                "payload_state_acceptance_criteria": len(criteria),
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"contract inventory validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
