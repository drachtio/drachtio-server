# RCA: VIB exit on Contact-less unreliable 180, then unanswered UPDATE

Date: 2026-10-22
Status: server fix committed and lab-verified. Orphan path not fixed.
Commit: `6a3411861c` on `fork/feature/early-dialog-update-routing`
Image verified: `drachtio-server-fixed:0.9.3-contactfix`

## Summary

Two different failures, and the TELUS capture only shows the second.

The crash is a 180 we generate, not one the carrier sends. `doRespondToSipRequest` added Contact only on the first 1xx, which the reliable 183 already had. The later unreliable 180 goes out with a To-tag and no Contact. RFC 3261 section 12.1.1 requires Contact on that response. On a call where the app sends the 200 before the PRACK, Sofia holds the 2xx and drachtio echoes that 180 back as the message it sent for the 200. `new Dialog` reads Contact off it, throws, and the throw is uncaught, so the VIB process exits. The TELUS capture is not that call: its PRACK completed 3.4s before the INVITE 200, and the carrier 180 already has Contact.

The orphan path is what the TELUS call shows. After the process exits, drachtio keeps the dialogs and the new process owns none of them. BYE on those dialogs is answered 481 by drachtio. UPDATE is not answered at all, so the carrier times the transaction out and sends 408, then a BYE that we also answer 481.

## Why UPDATE is not answered

`processRequestInsideDialog` (`sip-dialog-controller.cpp` around line 1747) forwards the request, then only generates a response when the client is gone for BYE:

```c
bool routed = route_request_inside_dialog(...);
if (!routed && dlg->getSipStatus() < 200) {
    // early dialog only: re-route UPDATE via route_request_inside_invite
}
addIncomingRequestTransaction(irq, transactionId);   // stores the irq, sends nothing
if (method == BYE && !routed)
    nta_incoming_treply(irq, SIP_481_NO_TRANSACTION);
```

`route_request_inside_dialog` (`client-controller.cpp` 354-399) looks the client up by dialog id. For UPDATE it also tries the UPDATE transaction id and the Call-ID. Both miss after the process exit: the new process owns neither the dialog nor the old INVITE transaction. It then logs `client managing dialog has disconnected`, calls `removeDialog` only for BYE and a terminated NOTIFY, and returns false.

A confirmed-dialog UPDATE whose client is gone is therefore stored in `m_mapTransactionId2Irq` and left there. Nobody calls `nta_incoming_treply`. Sofia retransmits the request. The carrier times the transaction out and answers 481 itself. That is why `vib_proxy_app.log` has 348 UPDATE CSeqs and zero 481 responses whose CSeq is UPDATE: the 481s in that log are all inbound (`<=== sip|network`), 57 PRACK and 7 BYE, sent by the carrier for transactions this side never answered.

The early-dialog branch does not apply to a confirmed call. It runs only when `dlg->getSipStatus() < 200`. A confirmed dialog is status 200, so the UPDATE is not re-routed and is not given the RFC 3311 500 either.

Two captured in `sipengine.log`. Neither got a response from this process:

- 2026-10-21 20:06:34.452Z, Call-ID `a6546f9f-482b-1240-20ba-00505693f978`, from-tag `aart9r7K8rr0Q`. `UPDATE lookup by Call-ID ... client: not found`, then `client managing dialog has disconnected`, then `addIncomingRequestTransaction`. Retransmitted at 20:06:37. Dialog had been up since 19:51:31.
- 2026-10-21 22:08:21.430Z, Call-ID `90d49434-483a-1240-20ba-00505693f978`, from-tag `0U31FZ7eK7c7a`. Same three lines. This is the TELUS call below. Dialog up since 21:38:17, UPDATE at 21:53:21 was answered, the next one 15 minutes later had no client.

Of the 53 `client managing dialog has disconnected` lines in that log, 44 are BYE (drachtio answers 481), 2 are UPDATE (not answered), 7 had no method on the adjacent line.

## What killed the client

`vib_proxy_app.log` (2026-10-20 14:29Z through 2026-10-22 05:52Z) records 29 process exits. Node prints the stack, the supervisor starts a new process, and the next line is a fresh `drachtio:agent` connect. The new process does not inherit the dialogs, so every call the dead process owned is orphaned at once.

21 exits are the Contact throw. First is 2026-10-20 14:52:41.826Z, Call-ID `41ad08ea148e878a37321499ea144675@10.189.5.107`, CSeq `121409 INVITE`, from `+19178176540` to `+19342332505`. The app sent a 200. The message drachtio echoed back was a 180 with no Contact:

```
SIP/2.0 180 Ringing
From: <sip:+19178176540@172.57.127.33;user=phone>;tag=10.189.5.107+5+edd35901+85a491e0
To: <sip:+19342332505@66.94.1.213;user=phone>;tag=28N54DHtQXHXN
Call-ID: 41ad08ea148e878a37321499ea144675@10.189.5.107
CSeq: 121409 INVITE
Content-Length: 0
```

```
Error: header 'Contact' not available in SIP message with Call-ID: 41ad08ea148e878a37321499ea144675@10.189.5.107
    at SipMessage.getParsedHeader (drachtio-srf/lib/sip-parser/message.js:102:13)
    at new Dialog (drachtio-srf/lib/dialog.js:72:39)
    at drachtio-srf/lib/srf.js:313:24
    at DrachtioAgent._onMsg (drachtio-srf/lib/drachtio-agent.js:799:11)
    at WireProtocol._onData (drachtio-srf/lib/wire-protocol.js:307:14)
Node.js v20.20.0
```

`wire-protocol.js:314` rethrows on the client socket, so the error is uncaught and the process exits. The same throw repeats at 18:12:31Z, 18:12:59Z, 19:49:35Z and 20:08:20Z on the 20th, and 13 times on the 21st through 2026-10-22 02:18:29Z (Call-ID `bae15fc72cf136d455902b3872eed298@10.189.5.107`). One of them, `f102a2982bcb326e7477715f54d66a30@10.189.5.107` at 18:38:15Z, is the dialog whose BYE hit `client managing dialog has disconnected` 32 seconds later.

8 exits are the reconnect, not the Contact bug. `Srf` forwards the agent `error` event with `setImmediate` (`srf.js:120`) and the image that produced these logs has no listener, so Node treats it as unhandled:

```
2026-10-20T17:32:14.567Z drachtio:agent wp#on close 127.0.0.2:9022
2026-10-20T17:32:14.826Z drachtio:agent wp#on error - Error: connect ECONNREFUSED 127.0.0.2:9022
node:events:502
      throw er; // Unhandled 'error' event
Error: connect ECONNREFUSED 127.0.0.2:9022
    at TCPConnectWrap.afterConnect [as oncomplete] (node:net:1611:16)
Emitted 'error' event on Srf instance at:
    at Immediate.<anonymous> (drachtio-srf/lib/srf.js:120:64)
```

Six are `ECONNREFUSED 127.0.0.2:9022` (17:32:14Z, 19:44:46Z, 19:49:59Z on the 20th; 15:22:16Z, 15:44:51Z, 17:32:27Z on the 21st). Two are `read ECONNRESET` (2026-10-21 14:59:18Z and 19:13:06Z), same `srf.js:120` frame. Each exits the process the same way the Contact throw does and leaves the same orphaned dialogs. The Contact fix does not stop these. `vib-proxy/src/app.js` does listen for `error`; the image that logged these did not.

## How the 180 lost its Contact

The Contact-less 180 is one we generate, not one the carrier sends. In the TELUS capture the carrier 180 already has Contact. The copy drachtio sends on toward VIB does not.

`telus_issue.txt` is synthesized from the application logs for one call, `+14169930894` to `+12898854661`, first packet 2026-10-21 21:38:16Z. Both PRACKs complete before the INVITE 200. The 200 is not sent early.

| t (s) | UTC | what |
| --- | --- | --- |
| 2.098 | 21:38:18.098 | carrier `75.152.144.247` sends reliable `183`, `Require: 100rel`, `RSeq: 2`, Contact present, Call-ID `0gQAAC8W...@169.60.125.137` |
| 2.102 | 21:38:18.102 | PRACK for that 183, `RAck: 2 121755556 INVITE` |
| 2.324 | 21:38:18.324 | `200 OK` for that PRACK, forwarded at 2.325 as Call-ID `90d49434-483a-1240-20ba-00505693f978` |
| 2.331 | 21:38:18.331 | carrier unreliable `180`, `Content-Length: 0`, Contact present, no RSeq |
| 2.332 | 21:38:18.332 | drachtio forwards that 180 toward VIB (`10.189.5.107` to `10.188.235.243`), Contact still present |
| 2.341 | 21:38:18.341 | drachtio sends the 180 on the B-leg toward VIB (`10.188.235.243` to `10.189.5.88:5066`), Call-ID `d553cf8db58da15896b9b8e309767cfc@10.189.5.88`, To-tag `77DyDBemaeDNe`, no Contact and no RSeq |
| 2.342 | 21:38:18.342 | VIB's own 180 back to drachtio, Call-ID `90d27e4a-483a-1240-20ba-00505693f978`, Contact `sip:10.189.5.88:5064` |
| 5.715 | 21:38:21.715 | carrier `200 OK` for the INVITE, 3.4s after the PRACK 200. Forwarded at 5.716, B-leg 200 at 5.727 |

So the setup order here is 183, PRACK, PRACK 200, 180, INVITE 200. Step 2 of the earlier write-up does not describe this call.

What the capture does show is the strip. Frame 34 still has `Contact: <sip:p65551t1792618696m986894c88105s1@10.189.5.107:5063;transport=udp>`. Frame 35, one hop later and 9ms later, is the same 180 with the To-tag kept and Contact gone:

```
SIP/2.0 180 Ringing
Via: SIP/2.0/UDP 10.189.5.88:5066;branch=z9hG4bK+83c2ef7c6893743c2a64b875be42a3213+sip+5+a6a6a7a2;rport=5066
From: <sip:14169930894@10.188.235.242:5060>;tag=10.189.5.88+5+215777ad+2ff65393
To: <sip:12898854661@10.189.5.88:5064;user=phone;xcsid=fe7e4051-5ead-4249-b818-2cbbdf477bad>;rp=MO;tag=77DyDBemaeDNe
Call-ID: d553cf8db58da15896b9b8e309767cfc@10.189.5.88
CSeq: 121755556 INVITE
Content-Length: 0
```

`doRespondToSipRequest` adds Contact only on the first 1xx of an INVITE transaction. The reliable 183 already took that. The later unreliable 180 is sent with the To-tag, which creates the early dialog, and without Contact. RFC 3261 section 12.1.1 requires Contact on that response. `6a3411861c` adds it on every such 1xx, which is the response frame 35 is missing.

The rest of this call shows the orphan path, not the Contact throw. This call did not crash VIB. The crash samples above are other calls.

- 21:53:21.840Z, UPDATE `CSeq: 238582420` on `90d49434-483a-1240-20ba-00505693f978` is answered `200` in 9ms. The dialog is live.
- 22:08:11.913Z, carrier BYE `CSeq: 3` on `p65541t1792618696m235130c66777s4`, `Reason: SIP;cause=480;text="Session-timer timeout (refresherRemote)"`. drachtio answers `481` in 3ms. `Server: SIP/2.0`, Contact `sip:169.60.125.137:5060`. That 481 is ours, not the carrier's.
- 22:08:22.028Z, carrier retransmits UPDATE `CSeq: 121755558` (238582421 on the VIB leg). No response from this side. Retransmits run to 22:08:38, then the carrier sends `408 Request Timeout` at 22:08:38.028Z and a BYE, which drachtio answers `481`.
- 22:09:28.863Z, VIB sends BYE on both its legs (`90d27e4a-...` and `d553cf8d...`). drachtio answers both `481`.

The 22:08:21 UPDATE is the second sample in the section above: `client managing dialog has disconnected`, then `addIncomingRequestTransaction`, no `nta_incoming_treply`.

## Where the echoed 180 comes from

Separate from the TELUS call, and this is the crash path. It needs the app to send the 200 before the PRACK, which this capture does not do.

1. UAS sends a reliable `183` with SDP and `Require: 100rel`, then an unreliable `180` with a To-tag and no Contact.
2. The app sends `200 OK` before the A-leg PRACKs the 183.
3. Sofia holds that 2xx. In `nta_incoming_mreply`, if `status >= 200` and `irq_reliable->rel_unsent` and the reliable provisional has a body, the final is queued in `reliable_final`. RFC 3262 section 3. Stock Sofia, not this patch.
4. `doRespondToSipRequest` still returns success and echoes `nta_incoming_getresponse(irq)`, the last response actually sent: the Contact-less 180.
5. `createUAS` builds `Dialog` from that echoed message. `getParsedHeader('Contact')` throws. `wire-protocol.js:314` rethrows. Process exits.
6. drachtio keeps the dialogs. The TCP client is gone. The next BYE is answered 481 by drachtio. The next UPDATE is stored and not answered.

## Fix, and what it does not fix

`doRespondToSipRequest` now adds Contact on every unreliable INVITE 1xx (`code > 100 && code < 200`), not only the first. Commit `ea69f29d19` added it only on the first 1xx, which the reliable 183 already had. The later 180 is the one Sofia echoes. `6a3411861c` adds it on every such 1xx. `100 Trying` is unchanged.

Client containment, so a missing Contact fails that call instead of exiting: `dialog.js` throws `Dialog: Contact header missing` before `getParsedHeader`, and `srf.js` `createUAS` rejects an echoed response that is not a 2xx with Contact.

Lab call `1-226605@172.31.240.211` on `drachtio-server-fixed:0.9.3-contactfix`: the 180 carried `Contact: <sip:172.31.240.211:5060>`, `session_refresh PASS`. That run sent the PRACK before the 200, which is the order the TELUS capture already has. The earlier lab failure sent the 200 before the PRACK, and that is a different bug: Sofia holds the 2xx in `reliable_final` until the reliable 183 with SDP is PRACKed, so the 200 is not on the wire however early the app sends it. The scenario change only avoids that queue. It does not fix it.

Not fixed:

- Sofia still holds the 2xx until the reliable 183 with SDP is PRACKed. Sending the 200 earlier does not put it on the wire. The TELUS call did not hit this. Its PRACK completed 3.4s before the INVITE 200.
- A dead client still orphans confirmed dialogs. The 8 `ECONNREFUSED` / `ECONNRESET` exits, a deploy, or an OOM leave the same state. UPDATE on that dialog is still stored and not answered.
- The timer H stall that filled the UDP 5060 receive queue (214016 bytes, lab call `1-213191`) was cleared by restarting sipengine. It is not fixed in this commit.
