# RCA: VIB exit on Contact-less unreliable 180, then 481 on orphaned dialogs

Date: 2026-10-22
Status: server fix committed and lab-verified. Residual orphan path not fixed.
Commit: `6a3411861c` on `fork/feature/early-dialog-update-routing`
Image verified: `drachtio-server-fixed:0.9.3-contactfix`

## Summary

An unreliable `180 Ringing` that creates an early dialog was sent with no Contact. RFC 3261 section 12.1.1 requires Contact on that response. drachtio later reported that 180 to the node client as the message it had sent for the 200 OK. `new Dialog` read Contact off it, threw, and the throw was uncaught, so the VIB process exited. Every dialog that client owned was orphaned. The next in-dialog request on those dialogs was parked or answered `481 Call/Transaction Does Not Exist`.

## Impact

Captured `vib_proxy_app.log` (2026-10-20 14:29Z through 2026-10-22 05:52Z) has 64 `481 Call/Transaction Does Not Exist` responses: 57 PRACK, 7 BYE.

- First PRACK 481: 2026-10-20 15:18:03Z, Call-ID `4859348f-473c-1240-ea88-00505693f978`, CSeq `121700950 PRACK`, from `+12028497226` to `+19177760861`.
- PRACK 481s continue through 2026-10-21 20Z (9 at 15Z, 10 at 16Z, 2 at 19Z, 3 at 23Z on the 20th; 3 at 14Z, 3 at 19Z, 7 at 20Z on the 21st; 20 more with no ISO timestamp on the same line).
- BYE 481s: 2026-10-20 20:19:25Z `f012427f60173dec2bc906af85c0902c@10.189.5.107`; 2026-10-21 13:49:24Z, 14:21:20Z, 14:34:30Z, 16:22:02Z, 19:12:29Z, plus one more.

`sipengine.log` (about 2026-10-21 18:16Z through 2026-10-22 05:47Z) has 53 `route_request_inside_dialog - client managing dialog has disconnected` and 16 `route_request_inside_invite - client managing transaction has disconnected`. A disconnected client is the state left after the process exit. Those requests are not answered immediately. BYE is the exception: `route_request_inside_dialog` drops the dialog and the stack answers 481. UPDATE and PRACK stay parked until the carrier times out (408, typically 32s).

## How it failed

1. UAS sends a reliable `183 Session Progress` with SDP and `Require: 100rel`, then an unreliable `180 Ringing` with a To-tag and no Contact.
2. The app sends `200 OK` before the A-leg PRACKs the 183.
3. Sofia holds that 2xx. In `nta_incoming_mreply` (`nta.c`), if `status >= 200` and `irq_reliable->rel_unsent`, `reliable_final` queues the final while an un-PRACKed reliable provisional still has a body. RFC 3262 section 3. This is stock Sofia, not introduced by the patch.
4. `doRespondToSipRequest` still returns success. The message it echoes is `nta_incoming_getresponse(irq)`, which is the last response actually sent: the Contact-less 180, not the queued 200. The wire log shows `Sending 200 response (not reliably)` and then `Msg sent:` followed by `SIP/2.0 180 Ringing` with no Contact.
5. `drachtio-srf` `createUAS` builds `Dialog` from that echoed message. `dialog.js` calls `getParsedHeader('Contact')`, which throws `header not available`. The throw is uncaught. Node exits.
6. drachtio keeps the dialogs. The TCP client is gone, so `findClientForDialog` fails. `route_request_inside_dialog` returns false. BYE is removed and answered 481. UPDATE/PRACK are not.

A second failure showed up in the lab once the 200 stayed queued. Timer G retransmitted the 183 (the log line is `retransmitFinalResponse`, but the bytes were the 183). At timer H (32s) `endRetransmitFinalResponse` ran and `processPrack` touched the orphaned reliable. The event loop stopped reading UDP 5060. The socket receive queue filled to 214016 bytes and the next INVITE was dropped. Restarting sipengine cleared it.

## Timeline

| Time (UTC) | Event |
| --- | --- |
| 2026-10-20 15:18:03 | First captured PRACK 481. Bursts continue through 2026-10-21 20Z. |
| 2026-10-20 20:19:25 | First captured BYE 481, Call-ID `f012427f...@10.189.5.107`. |
| 2026-10-21 18:16 to 2026-10-22 05:09 | 53 in-dialog requests hit a disconnected client in `sipengine.log`. |
| 2026-10-22 08:56Z | `vib_proxy_app` restarted (restart count 5) after the Contact-less Dialog throw. |
| 2026-10-22 08:59:58 | Lab call `1-213191@172.31.240.211`. 200 submitted on irq `0x55b94f851180`. Echoed message was the 180. Timer G retransmitted the 183 (1250 bytes) at 08:59:59 and 09:00:00. |
| 2026-10-22 09:00:30 | Timer H: `endRetransmitFinalResponse - never received ACK`, then `processPrack`. Event loop stopped reading 5060. |
| 2026-10-22 09:04:50 | Next SIPp INVITE dropped. Host socket `172.31.240.211:5060` rx queue 214016. |
| 2026-10-22 09:05:53 | sipengine restarted on `drachtio-server-fixed:0.9.3-contactfix`. Queue empty, client reconnected. |
| 2026-10-22 09:06:02 | Call `1-226605@172.31.240.211` completed. 180 carried `Contact: <sip:172.31.240.211:5060>`. |
| 2026-10-22 09:14:30 | Fix committed `6a3411861c` and pushed to `goolanceman/drachtio-server`. |

Local lab times above are IST (UTC+5:30) converted to UTC. The 08:59 and 09:06 timestamps are the sipengine log clock, which is UTC.

## Fix

`src/sip-dialog-controller.cpp`, unreliable branch of `doRespondToSipRequest`. Contact is added on every dialog-forming unreliable INVITE response, not only the first 1xx:

- 2xx, as before
- every unreliable 1xx with `code > 100 && code < 200` on an INVITE

`100 Trying` still has no Contact. A client-supplied Contact is still preferred. Reliable 1xx already added Contact via `nta_reliable_treply`.

Adding Contact only on the first 1xx (commit `ea69f29d19`) was not enough. A reliable 183 already has Contact. The later unreliable 180 often does not, and that 180 is the message Sofia echoes. `6a3411861c` adds it on every such 1xx.

Client-side containment, already in tree, so a missing Contact fails the call instead of exiting the process:

- `vib-proxy`, `cl-voice-application`, and `clv-legacy` `src/drachtio-srf-patches/lib/dialog.js`: throw `Dialog: Contact header missing` instead of `getParsedHeader` throwing `header not available`.
- `srf.js` `createUAS`: reject the echoed message unless it is a 2xx with Contact, and catch a Dialog constructor failure.

## Verification

`sipp/suite/session_refresh` against the patched image, call `1-226605@172.31.240.211`:

- UAC order: INVITE, 100, 183, 180 with Contact, PRACK, 200 PRACK, 200 INVITE, ACK, BYE, 200 BYE.
- Suite result: `session_refresh PASS successful call 1`.
- `vib_proxy_app` stayed up. Port 5060 receive queue stayed empty.

The scenario was changed so this is reachable. `uac.xml` PRACKs the 183 before it waits for the 200. `uas.xml` waits 1500 ms after the 180 before the 200. A late PRACK cannot release a 2xx Sofia has already queued behind an unacknowledged reliable provisional with SDP. The first rerun after that edit timed out only because the stuck sipengine was still bound to 5060 and not reading it.

## What this does not fix

- Sofia still queues the 2xx until the reliable 183 with SDP is PRACKed. The app must not send the 200 before that PRACK. Releasing the 2xx without a PRACK would violate RFC 3262.
- A dead client still orphans confirmed dialogs. Deploy, OOM, or any other uncaught exception leaves the same 481/408 path. `route_request_inside_dialog` still parks UPDATE and PRACK on a disconnected client and only answers 481 for BYE.
- The timer H / `processPrack` stall that filled the 5060 receive queue was cleared by restart. It is not fixed in this commit.
