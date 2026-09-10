# Verified offline overlay archive — 2026-09-10

The host fell below the 1.5-GiB new-build free-space floor. Only the following
old, task-owned, offline overlay is selected for archiving; current/last-good
guest disks, the parent base, other projects and user-owned local Build/ are
excluded.

- Original: `/home/dima/Projects/itlwm/pmf-live-20260724c/tahoe-pmf-runtime.qcow2`
- Archive host: `dima@10.7.6.112`
- Archive directory: `/home/dima/Projects/itlwm-runtime-archive/scan-replay-20260910.OxJ3Fx`
- Logical file length: `1902247936` bytes; qcow2 virtual capacity: 100 GiB.
- SHA-256: `6d64800a35ebc073bdf42a79e0660279dd05efcccb2bd95a4d35cfe01dcaf240`
- Unchanged backing file: `/home/dima/Projects/itlwm/tahoe.qcow2`.

The byte-copy rsync completed successfully: 1,902,247,936 literal bytes,
528,136,148 compressed transfer bytes. Independent source and remote SHA-256
and remote file length match. A read-only qcow2 header census of 379 files
under Projects found no image backed by this overlay. A final open-file check
is required immediately before local removal.

Its OVMF variables, attestation and three logs are copied to the same archive;
the originals of these small companion files are retained locally. Copying
does not rewrite qcow2 metadata or change its backing relationship.

Restore to the original path, after checking that path is absent and unused:

```sh
rsync -aS -e 'ssh -o BatchMode=yes -o ConnectTimeout=5' \
  dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/scan-replay-20260910.OxJ3Fx/tahoe-pmf-runtime.qcow2 \
  /home/dima/Projects/itlwm/pmf-live-20260724c/tahoe-pmf-runtime.qcow2
sha256sum /home/dima/Projects/itlwm/pmf-live-20260724c/tahoe-pmf-runtime.qcow2
```

Local removal completed after both `lsof` and `fuser` found no open user.
Only the exact qcow2 above was removed. It remains recoverable from the
verified archive; all five companion files and the parent base remain local.
After ZFS accounted for the deletion, host free space was 2,418,933,760 bytes
(about 2.25 GiB), above the 1.5-GiB new-build floor. No snapshot was changed.
