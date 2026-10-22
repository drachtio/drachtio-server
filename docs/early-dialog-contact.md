# Contact on unreliable early-dialog responses

## Problem

An unreliable `180 Ringing` (or `183`) that carries a To-tag creates an early dialog. RFC 3261 section 12.1.1 requires a Contact on that response. The unreliable path in `SipDialogController::doRespondToSipRequest` only added Contact on a 2xx. Reliable 1xx already added one.

That missing Contact crashed the node client. `doRespondToSipRequest` reports the message returned by `nta_incoming_getresponse` as the sent response. After a 200 OK that Sofia has queued, that message is often the earlier unreliable 180, not the 200. `drachtio-srf` builds `Dialog` from it and throws `header not available` when Contact is absent. The throw is uncaught, the process exits, and every dialog that client owned is orphaned. Later in-dialog requests (UPDATE, BYE) then get `481 Call/Transaction Does Not Exist` or sit until the carrier times out with 408.

Adding Contact only on the first 1xx is not enough. A reliable 183 already has Contact, but a later unreliable 180 often does not, and that 180 is the message Sofia echoes.

## Fix

In `src/sip-dialog-controller.cpp`, the unreliable reply path adds Contact on every dialog-forming response for an INVITE:

- 2xx, as before
- every unreliable 1xx other than `100 Trying` (`code > 100 && code < 200`)

`100 Trying` is unchanged and still has no Contact. A client-supplied Contact is still used instead of the generated one. Reliable 1xx already adds Contact in the `nta_reliable_treply` path above this.

## What this does not change

Sofia queues a 2xx in `reliable_final` (`nta.c`) while an un-PRACKed reliable provisional still carries SDP (RFC 3262). Until that PRACK arrives, `nta_incoming_getresponse` still returns the previous 1xx, and timer G retransmits that provisional. The echoed message must therefore be a valid dialog-forming response. This fix makes that 180 valid. It does not release the queued 200 without a PRACK, and it does not answer an in-dialog request that arrives after the client has already died.

## Verification

Image `drachtio-server-fixed:0.9.3-contactfix`, SIPp `session_refresh` call `1-226605@172.31.240.211`:

- UAC received `180 Ringing` with `Contact: <sip:172.31.240.211:5060>`
- UAC PRACKed the reliable 183 before the UAS sent the 200, so Sofia released the queued 2xx
- Sequence completed: 183, 180 with Contact, PRACK, 200 PRACK, 200 INVITE, ACK, BYE, 200 BYE
- `vib_proxy_app` stayed up; port 5060 receive queue stayed empty
