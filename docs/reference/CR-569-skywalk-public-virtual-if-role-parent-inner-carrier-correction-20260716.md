# CR-569 — Skywalk public VIRTUAL_IF_ROLE/PARENT inner-carrier correction

The 25C56 public ROLE and PARENT GET/SET wrappers recovered in CR-527 are
direct 11-byte leaves returning 0xe082280e. Their bodies read neither public
argument.

The earlier Tahoe source guard additionally tested req->req_data for non-null.
That test is itself an observable inner-carrier read and made the result depend
on information the reference does not inspect. CR-569 removes only that
predicate. After the pre-existing outer req == NULL fallback, Tahoe GET or SET
for IOC 96 or 97 returns the raw fixed status without reading req->req_data.

The correction does not change outer-null dispatch, pre-26 behavior, the
fallback logic after the Tahoe guard, carrier declarations, the card-specific
policy, V1 code, allocation, APSTA ownership, radio, association, traffic, or
runtime selector invocation. The CR-527 reference artifact and report now
explicitly validate the no-inner-carrier-read boundary.
