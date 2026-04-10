# Wi-Fi reverse YAML bundle v9

This bundle extends v8 with a tighter owner-binding and descriptor-origin recovery pass.

Added:
- 41_commonfsm_descriptor_origin_candidates.yaml
- 42_wclscanmanager_owner_recovery.yaml
- 43_second_owner_candidate_matrix.yaml
- 44_symbolic_lift_execution_plan.yaml
- 45_v9_done_vs_remaining.yaml

Focus:
- convert the remaining gap into a deterministic metadata-lifting problem
- lock WCLScanManager as the first semi-symbolic CommonFsmManager owner
- define the shortest path to the first fully symbolic FSM

Important:
- This is still a static-kernel bundle.
- The main blocker is not missing binaries anymore.
- The main blocker is descriptor origin recovery + rodata/vector lift + action callback naming.
