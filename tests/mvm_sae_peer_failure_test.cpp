// Execute the same failure/retirement scenarios against the exact MVM owner
// and worker extracted by test_mvm_sae_peer_failure.sh. Family spelling alone
// is normalized; lower hardware, crypto and scheduling are fixture boundaries.
#define SAE_JOIN_TEST_FAMILY "IWM/IWX"
#define SAE_JOIN_TEST_ADMISSION 1
#define SAE_JOIN_TEST_TX_RETIREMENT 1
#include "tests/iwn_sae_join_failure_test.cpp"
