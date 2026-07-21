/*
 * Test-only entry point for upstream hostap common_module_tests.c:sae_tests().
 * The upstream routine succeeds only after these group-19 vectors pass:
 *   - HnP Commit, KCK, PMK, and PMKID;
 *   - H2E PT-to-PWE x/y.
 *
 * It is never compiled into AirportItlwm or AirportItlwmAgent.
 */
#include <stdio.h>

int sae_tests(void);

int
main(void)
{
	if (sae_tests() != 0) {
		fputs("hostap SAE group-19 HnP commit/KCK/PMK/PMKID KAT: FAIL\n",
		      stderr);
		fputs("hostap SAE group-19 H2E PT/PWE x/y KAT: FAIL\n", stderr);
		return 1;
	}

	fputs("hostap SAE group-19 HnP commit/KCK/PMK/PMKID KAT: PASS\n",
	      stdout);
	fputs("hostap SAE group-19 H2E PT/PWE x/y KAT: PASS\n", stdout);
	return 0;
}
