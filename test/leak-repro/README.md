# leak-repro

Reproducer for the "transport gone before response" `nta_incoming_t` leak
in drachtio-server. Used to verify the fix in
`src/sip-dialog-controller.cpp` that destroys the irq even when the
underlying transport has already been shut down.

## Scenario

```
UAC                                  drachtio                       app
 |   INVITE                              |                            |
 |--------------------------------------->|---- INVITE --------------->|
 |                                       |<--- 200 OK ----------------|
 |<-------------------------- 200 OK ----|                            |
 |   ACK                                 |                            |
 |--------------------------------------->|---- ACK ------------------>|
 |   (--call-duration delay)             |                            |
 |   BYE                                 |                            |
 |--------------------------------------->|---- BYE ------------------>|
 |   *** RST socket immediately ***                                   |
 |                                       |        (--app-bye-delay)   |
 |                                       |<--- 200 OK to BYE ---------|
 |
 |   drachtio tries to forward 200 OK upstream → transport gone
```

To make drachtio see the transport as gone *before* the BYE response
arrives, the app needs to delay its 200 OK to BYE long enough for the
RST to propagate through the kernel and for Sofia to run
`tport_close`/`tport_shutdown0` on the relevant secondary tport. In
practice 3–10 seconds is more than enough; 500 ms is borderline.

## Build

```
go build -o leak-tester .
```

## Run

TCP (default port 5060):

```
./leak-tester --drachtio 127.0.0.1:5060 --callee 15083084809
```

Plain WS:

```
./leak-tester --drachtio host:port --transport ws --callee 15083084809
```

WSS (skips cert verify by default):

```
./leak-tester --drachtio host:port --transport wss --callee 15083084809
```

If the app's answering Contact rewrites the host/port/transport (as
jambonz feature-server does), pass `--reuse-connection` to strip the
Contact from the 200 OK so ACK/BYE go back on the original socket.

## Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `--drachtio` | `127.0.0.1:5090` | drachtio listen addr (host:port) |
| `--transport` | `tcp` | `tcp`, `ws`, or `wss` |
| `--insecure` | `true` | skip TLS cert verify for `wss` |
| `--callee` | `test@127.0.0.1` | request URI (`user@host` or just user) |
| `--from` | `leak-tester` | From user |
| `--bind` | empty | local bind addr (`host:port`) |
| `--call-duration` | `5s` | wait between ACK and BYE |
| `--reuse-connection` | `false` | ignore 200 OK Contact, send ACK/BYE on original connection |
| `--loop` | `1` | run N iterations; each opens a fresh UA/connection. Useful for stress-testing the leak fix. |
| `--loop-delay` | `2s` | delay between iterations when `--loop > 1` (lets the prior RST + 5s app delay settle before the next call) |
| `--quiet` | `false` | suppress sipgo internal logging |

## Verifying a leak

Run drachtio with `--memory-debug-time 10` (or similar) and watch the
watchdog dump for orphan irqs after the test exits. With the fix in
place, the per-call dump should show:

- `m_mapTransactionId2Irq size: 0`
- `Irqs: 0 total, 0 orphaned` in the leak detection report
- The relevant `tport_zap_secondary` line for the closed transport

Before the fix, the BYE's irq would persist with `alive N secs`
growing on each watchdog tick until process exit.
