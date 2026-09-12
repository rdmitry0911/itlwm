# Native scan supersedes an accepted roam — 2026-09-12

Status: preserved dependency; the user has moved the actual GUI connection
matrix to first priority. An ordinary scan-only CoreWLAN request reproduces cancellation of a
real accepted IWN roam on the published c123d132/D636 image. The source link
survives, but the requested target transition fails. No production correction
is claimed here. The complete WCL progress/terminal contract is the next
implementation boundary; merely adding a start event would leave its consumer
without a real completion.

## Baseline and bounded controls

The preceding post-S3 reverse request at03:19:34 UTC failed before target AUTH,
with SUPERSEDED_BY_WCL_REQUEST and source250/250 each way. Its trace did not
observe scan/associate ingress, so the newer request's exact origin is unknown.
A later historical airportd query retained2115 lines but no scan/roam events.
Neither that absence nor a roughly5s interval establishes an upper timeout.
The recovered WCLRoamManager protection timer is10000ms, not5000ms.

The unchanged guest retains boot900A4367-43E5-42DC-8DC4-C0662A9E954E and
UUID D636A28B-6A9B-3CCE-AF28-5779C5F980C8. New synchronous function/caller
observations distinguish public scan, public association, leave, owned failure
and the actual WCL consumers. The added probes read no private object layout,
scan payload, credential or node; the existing SAE metadata observer is reused.
An ordinary bounded airportd live stream supplies userland request attribution.

1. Native departure q1 at03:39:50 UTC requests LabAP02/channel13 -> ca/channel9.
   Target preparation starts03:39:55.747; one SAE exchange reaches ASSOC/RUN.
   Traffic is249/250 forward and248/250 reverse. The90s observer ends with
   errors0, encap_drops1, public_scans0, public_associations0, cancellations0.
   It observes handleReassocEvent, but no handleRoamStartEvent,
   handleRoamScanEnd, handleRoamPrepEvent or handleRoamDoneEvent. This is a
   target success with retained loss, not reproduction of the scan overlap.
2. Scan-only overlap q1 requests ca/channel9 ->02/channel13 at03:43:50 UTC.
   A separate public scanForNetworksWithName:nil starts after the successful
   request return and250ms spacing. It never joins, toggles power, changes
   credentials or invokes a private driver function.
   Actual WCL_REASSOC at1789184630869339004ns starts physical scan410.
   At1789184631434340020ns, WCLScanManager::sendRequest enters setWCL_SCAN_REQ.
   Its synchronous nested cancellation records scan=1/associate=0/result89
   (Darwin ECANCELED), and retires accepted serial25. WCL consumes
   setReassocFail. The pending handoff starts real2GHz scan411, followed by
   5GHz scan412. No target AUTH/SAE/RUN occurs.
   The scan helper succeeds with8 records; its LabAP count0 is not an absence
   finding: airportd explicitly filters SSID/BSSID because this process lacks
   location authorization. The driver observations establish fresh physical
   scans independently of the privacy-filtered returned identities.
   Final BSSID staysca with WPA3/DHCP.219 and250/250 packets each direction.
   The controller returns1 at its target assertion; observererrors0,
   encap_drops0, public_scans2, public_associations0, cancellations1.

Both complete90s observers and105s log streams have terminated. The log stream
is bounded by an exec-inherited alarm; SSH returns255 when that process dies,
not a successful log command. Complete output and that status are retained.
No repeated roam is issued to relabel either outcome. The originally failed
reverse run is not retroactively assigned the controlled run's caller/timing.
Physical10.90.10.22, QEMU lifecycle, disks, host radio configuration and the
installed kext are unchanged.

## Reference contract and remaining debt

The updated5995e24caa exports show WCLScanManager::sendRequest consulting
isScanAllowedByOtherActivity before invoking the driver. It distinguishes
join/GAS/NDD/IP-resolution activity, a3000ms high-priority-reassociation
window, low-RSSI roaming with a low-latency-scan exception, and link-loss
suppression. This is not a universal prohibition on scans during roaming.
getRoamState reports actual FSM activity separately from pending/high-priority
explicit request flags. Therefore neither unconditional scan rejection nor
merely extending a timeout reproduces this contract.

The current Intel setter explicitly cancels every still-cancellable accepted
roam before associated public scan admission. Its premise that an ordinary
escan is equivalent to a new join must be revalidated. Separately, the current
controller publishes only0x49 success or0xcf failure for accepted roams. It
lacks the recovered0x89/0x8a/0x8b progress and0x50 overall completion chain.
The exact WCL FSM consumes0x89 to enter ROAM_SCAN;0x50, not0xcf, executes
roamDone and releases timer/request/policy bookkeeping. The observed native
success consequently lacks that full consumer sequence.

The earlier51-function package provides the lifecycle table and core event
builders, but does not establish the complete216-byte scan-end producer or
all168-byte completion fields. Also, existing startEventScan exports still
contain falsely non-returning memcpy calls. The exact nm labels identify
ffffff8000101050 as memcpy, as distinct from memmove atffffff8000101080.
A fresh83-function,40-interface read-only batch includes adjacent scan event
builders, request arbitration, completion producers and consumers; it corrects
that returning-helper annotation without saving the reference database.
Its results must be inspected before claiming field-level completeness or
implementing the full lifecycle. No driver patch is justified by the absence
of an event alone without its real producer and matching terminal ownership.

## Evidence

Working root:/dev/shm/aiam-roam-supersession-20260912.jgZHsA.
Reference batch:/home/dima/Projects/ghidra_output/aiam-roam-supersession-5995e24caa-20260912.WYYx6P
on10.7.6.112. Current source documentation HEAD is1d892231; production remains
c123d132. Older completed RF archives remain read/copy-only.

- Native successful control traceSHA256:
  30e47d786bd44d87b4e380f141cda9c0f425baba5af2b0e6f8a3c2152bf8c522.
- Failed scan-only overlap traceSHA256:
  9b75e086d9ff2d62afe1376bb9a49cc3aed55358d301e19b834cd893c04265c5.
- Corresponding airportd live streamSHA256:
  9892357a0061c1ff1dce5bead4058c2bf49681035a1fe9b5b46e7373acb09b0c.

## Completed reference batch and independently decoded fields

The83-function batch terminated03:53:05UTC. All83 entries are complete and
the manifest records40 actual interfaces. ManifestSHA256:
ee55c6d322e733554e3b8de36e6e57bb7b6af88be2688bbede8603df26a0e96d.
The additional returning memcpy annotation recovers the previously truncated
startEventScan branches; its complete output is1564 lines. None of these
results is a production or RF correction.

The direct raw-file vtables for AppleBCMWLANBSSBeacon, WCLBSSBeacon and
IO80211BSSBeacon resolve the completion capability flags: bit0 neighbor
reports,1 fast transition,2 BSS transition management,3 CCX80211r,4 QoS
FastLane,5 CCX network assurance,6 computed score. Bit7 is the separately
computed colocated-roam result. These are not an encryption/PMF flags bitmap.
Raw vtable reportSHA256:
644f8cb090e60d37168a7bfe840d67699da9ccfd10720e4c221cf25cb231b37c.

The Ghidra instruction census sees unowned/unanalysed code and therefore is
not an exhaustive producer search. A separate original-byte Capstone pass
decodes all5202 exact nm-bounded AppleBCMWLAN functions, checks full byte
consumption and retains direct postMessage call windows and relevant fields.
ReportSHA256:
bed3dadb87befce5bdbbe28bb7a2b300e5e1f67ef81a5897cf6afbd4d0e425db.
No literal0x8a publication was found in that pass. This is narrower than proof
of absence of a dynamically selected producer. The216-byte WCL consumer is
real, but its registration alone does not prove that every Broadcom roam must
emit scan-end or that a missing scan-end is the cause of this failure.
The required start/preparation/terminal lifecycle is independently established
by its actual producers and FSM. Do not fabricate optional events merely to
satisfy an assumed fixed event list. Remaining tail-field ownership and Intel
scan arbitration still require implementation-level reconciliation.

## Executed full-lifecycle requirements, intentionally failing

The ordinary25-case admission/abort/retirement/controller suite remains green
on Linux ASan/UBSan. It formerly asserted only the legacy selector and copied
payload, not WCL consumer completion. The new reference consumer oracle encodes
all60 raw FSM cells and distinguishes0xcf bookkeeping from0x50/roamDone.
Its calibration includes successful completion, failures with/without target
preparation, rejected payload length, and timeout not substituting for done.

WCL_REASSOC_REQUIRE_LIFECYCLE=1 scripts/test_wcl_reassoc_failure_retirement.sh
executes the actual complete common admission/retirement and controller
publication bodies. All four independent requirements fail with exit134:

1. Accepted lower scan: no0x89; consumer stays LINK_UP, start count0.
2. Failed census after a reference-side started precondition:0xcf clears the
   pending bit, but consumer stays ROAM_SCAN with timer active and done count0.
3. Successful target after a reference-side prepared precondition:0x49 moves
   to WAIT_ROAM_DONE; timer/pending remain and done count0.
4. Failed prepared target:0xcf leaves ROAM_REASSOC/timer active, done count0.

The three terminal-only cases explicitly seed only the consumer precondition;
they do not claim that current production emitted those start/prep events.
Physical scan, selection, crypto and epoch callbacks are fixture boundaries,
not hardware execution. The required wrapper exits1, and is deliberately not
folded into the passing aggregate. No Tahoe execution of these new cases has
yet been claimed. Linux full requirement logSHA256:
8470a9ed6571c5b8332c8c81060138a16e84620d64d0b3e2de9ad66297aede4e.

No production source, loaded image, release, radio configuration or QEMU
lifecycle was changed for this requirement. Further independent research is
now deferred to the GUI-first ledger; no live batch/observer remains.
