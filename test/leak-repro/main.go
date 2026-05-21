// SIP UAC test client that triggers the "transport gone before response" leak
// in drachtio-server.
//
// Sequence:
//
//	UAC                                  drachtio                       app
//	 |   INVITE (TCP)                       |                            |
//	 |------------------------------------->|---- INVITE (TCP) --------->|
//	 |                                      |<--- 200 OK ----------------|
//	 |<------------------------- 200 OK ----|                            |
//	 |   ACK                                |                            |
//	 |------------------------------------->|---- ACK ------------------>|
//	 |   (--call-duration delay)            |                            |
//	 |   BYE                                |                            |
//	 |------------------------------------->|---- BYE ------------------>|
//	 |   *** Close TCP socket immediately ***                            |
//	 |   X                                  |                            |
//	 |                                      |              (--app-bye-delay)
//	 |                                      |<--- 200 OK to BYE ---------|
//	 |                                      |
//	 |                       drachtio tries to forward 200 -> tport gone
//	 |                                      |
//
// Detection: after the run, inspect drachtio's --memory-debug output looking
// for a leaked nta_incoming_t* entry whose method/cseq match the BYE.
package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"log/slog"
	"net"
	"os"
	"reflect"
	"strings"
	"time"

	"github.com/emiago/sipgo"
	"github.com/emiago/sipgo/sip"
)

func main() {
	var (
		drachtioAddr    = flag.String("drachtio", "127.0.0.1:5090", "drachtio listen addr (host:port)")
		transport       = flag.String("transport", "tcp", "transport to use: tcp, ws, or wss")
		insecure        = flag.Bool("insecure", true, "skip TLS cert verification for wss")
		callee          = flag.String("callee", "test@127.0.0.1", "request URI user@host")
		fromUser        = flag.String("from", "leak-tester", "From user")
		bindAddr        = flag.String("bind", "", "local bind addr (host:port, port=0 for ephemeral); empty = let OS choose")
		callDuration    = flag.Duration("call-duration", 5*time.Second, "time to wait between ACK and BYE")
		reuseConnection = flag.Bool("reuse-connection", false, "send ACK/BYE on the original connection (ignore 200 OK Contact)")
		loop            = flag.Int("loop", 1, "number of iterations to run (each uses a fresh UA/connection)")
		loopDelay       = flag.Duration("loop-delay", 2*time.Second, "delay between iterations when --loop > 1")
		quiet           = flag.Bool("quiet", false, "suppress sipgo internal logging")
	)
	flag.Parse()

	tp := strings.ToLower(*transport)
	switch tp {
	case "tcp", "ws", "wss":
	default:
		fmt.Fprintf(os.Stderr, "FAIL: invalid --transport %q (want tcp, ws, or wss)\n", *transport)
		os.Exit(1)
	}

	logLevel := slog.LevelDebug
	if *quiet {
		logLevel = slog.LevelWarn
	}
	slog.SetDefault(slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: logLevel})))

	n := *loop
	if n < 1 {
		n = 1
	}

	var passed, failed int
	for i := 1; i <= n; i++ {
		if n > 1 {
			slog.Info("iteration start", "i", i, "of", n)
		}
		err := run(*drachtioAddr, tp, *insecure, *callee, *fromUser, *bindAddr, *callDuration, *reuseConnection)
		if err != nil {
			failed++
			fmt.Fprintf(os.Stderr, "FAIL iter %d/%d: %v\n", i, n, err)
		} else {
			passed++
			if n > 1 {
				fmt.Printf("OK iter %d/%d\n", i, n)
			} else {
				fmt.Println("OK: BYE sent and socket closed immediately")
			}
		}
		if i < n && *loopDelay > 0 {
			time.Sleep(*loopDelay)
		}
	}

	if n > 1 {
		fmt.Printf("\nSUMMARY: %d passed, %d failed (of %d iterations)\n", passed, failed, n)
	}
	if failed > 0 {
		os.Exit(1)
	}
}

func run(drachtioAddr, transport string, insecure bool, callee, fromUser, bindAddr string, callDuration time.Duration, reuseConnection bool) error {
	var uaOpts []sipgo.UserAgentOption
	if transport == "wss" {
		uaOpts = append(uaOpts, sipgo.WithUserAgenTLSConfig(&tls.Config{
			InsecureSkipVerify: insecure,
		}))
	}
	ua, err := sipgo.NewUA(uaOpts...)
	if err != nil {
		return fmt.Errorf("NewUA: %w", err)
	}
	defer ua.Close()

	var clientOpts []sipgo.ClientOption
	if bindAddr != "" {
		clientOpts = append(clientOpts, sipgo.WithClientConnectionAddr(bindAddr))
	}
	cli, err := sipgo.NewClient(ua, clientOpts...)
	if err != nil {
		return fmt.Errorf("NewClient: %w", err)
	}

	contactHost := hostOf(bindAddr)
	if contactHost == "" {
		contactHost = "127.0.0.1" // sipgo overwrites this with actual local addr at send time
	}
	contactHdr := sip.ContactHeader{
		Address: sip.Uri{User: fromUser, Host: contactHost},
	}
	dlgCli := sipgo.NewDialogClientCache(cli, contactHdr)

	host, port := splitHostPort(drachtioAddr)
	uriParams := sip.NewParams()
	uriParams.Add("transport", transport)
	target := sip.Uri{
		User:      userOf(callee),
		Host:      host,
		Port:      port,
		UriParams: uriParams,
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// --- INVITE ---
	slog.Info("sending INVITE", "to", target.String())
	sdp := minimalSDP(hostOf(bindAddr))
	sess, err := dlgCli.Invite(ctx, target, sdp,
		sip.NewHeader("Content-Type", "application/sdp"))
	if err != nil {
		return fmt.Errorf("Invite: %w", err)
	}
	defer sess.Close()

	if err := sess.WaitAnswer(ctx, sipgo.AnswerOptions{}); err != nil {
		return fmt.Errorf("WaitAnswer: %w", err)
	}
	if sess.InviteResponse.StatusCode != sip.StatusOK {
		return fmt.Errorf("INVITE got non-200: %d", sess.InviteResponse.StatusCode)
	}
	slog.Info("got 200 OK to INVITE", "callid", sess.InviteRequest.CallID().Value())

	if reuseConnection {
		// Strip Contact from the 200 OK so sipgo's ACK/BYE builders fall back
		// to the INVITE Recipient (original drachtio addr + transport=wss).
		// Without this, jambonz' Contact rewrite sends in-dialog requests to
		// a different port/transport and the ACK hangs.
		if sess.InviteResponse.RemoveHeader("Contact") {
			slog.Info("stripped Contact from 200 OK (reuse-connection)")
		}
	}

	// --- ACK ---
	if err := sess.Ack(ctx); err != nil {
		return fmt.Errorf("Ack: %w", err)
	}
	slog.Info("ACK sent")

	// --- wait, then BYE + immediate socket close ---
	slog.Info("call established, sleeping before BYE", "duration", callDuration)
	time.Sleep(callDuration)

	// Find sipgo's underlying TCP connection to drachtio so we can RST it as
	// soon as the BYE is on the wire. We dig into the unexported Conn field
	// via reflection because sipgo's Connection interface only exposes
	// graceful Close(). For WSS the underlying net.Conn is a *tls.Conn —
	// unwrap one more level to get to the TCPConn.
	tcpConn := findRawTCPConn(ua.TransportLayer(), transport, drachtioAddr)
	if tcpConn == nil {
		slog.Warn("could not locate raw *net.TCPConn; will rely on graceful close")
	}

	slog.Info("sending BYE")
	byeCtx, byeCancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer byeCancel()

	// Fire BYE in background. We don't care about its response — the whole
	// point is that drachtio's response will arrive after the socket is gone.
	byeDone := make(chan error, 1)
	go func() { byeDone <- sess.Bye(byeCtx) }()

	// Give the BYE bytes a moment to flush to the kernel before we yank the
	// socket. 10ms is enough; tune up if you ever see the BYE not making it.
	time.Sleep(10 * time.Millisecond)

	if tcpConn != nil {
		// SetLinger(0) + Close() = RST. drachtio's tport notices immediately.
		_ = tcpConn.SetLinger(0)
		if err := tcpConn.Close(); err != nil {
			slog.Debug("RST close returned err (ok)", "err", err)
		}
		slog.Info("TCP socket RST")
	}

	// Wait for the BYE goroutine to unwind (it will error — that's fine).
	<-byeDone
	return nil
}

// findRawTCPConn locates the *net.TCPConn that sipgo is using to talk to raddr
// and returns it for direct manipulation. Returns nil if it can't be found.
//
// We need this because sipgo's Connection interface only exposes graceful
// Close(). For this test we want SetLinger(0)+Close() (RST) so drachtio sees
// the transport go away the instant we want it to, with no FIN-wait dance.
//
// For tcp/ws, the Conn field on sipgo's *TCPConnection / *WSConnection is the
// raw *net.TCPConn. For wss, the Conn field is a *tls.Conn whose underlying
// connection (NetConn()) is the *net.TCPConn we want.
func findRawTCPConn(tl *sip.TransportLayer, transport, raddr string) *net.TCPConn {
	c, err := tl.GetConnection(transport, raddr)
	if err != nil {
		return nil
	}
	v := reflect.ValueOf(c)
	if v.Kind() == reflect.Ptr {
		v = v.Elem()
	}
	if v.Kind() != reflect.Struct {
		return nil
	}
	f := v.FieldByName("Conn")
	if !f.IsValid() {
		return nil
	}
	inner := f.Interface()
	if tc, ok := inner.(*net.TCPConn); ok {
		return tc
	}
	if tlsConn, ok := inner.(*tls.Conn); ok {
		if tc, ok := tlsConn.NetConn().(*net.TCPConn); ok {
			return tc
		}
	}
	return nil
}

func splitHostPort(addr string) (string, int) {
	h, p, err := net.SplitHostPort(addr)
	if err != nil {
		return addr, 5060
	}
	var port int
	fmt.Sscanf(p, "%d", &port)
	return h, port
}

func hostOf(addr string) string {
	h, _ := splitHostPort(addr)
	return h
}

// minimalSDP returns a barebones audio offer (PCMU). drachtio apps generally
// want SDP on an INVITE so they can answer with their own. The IP/port don't
// need to be reachable — we never send media.
func minimalSDP(host string) []byte {
	if host == "" {
		host = "127.0.0.1"
	}
	body := "v=0\r\n" +
		"o=- 0 0 IN IP4 " + host + "\r\n" +
		"s=leak-repro\r\n" +
		"c=IN IP4 " + host + "\r\n" +
		"t=0 0\r\n" +
		"m=audio 4000 RTP/AVP 0\r\n" +
		"a=rtpmap:0 PCMU/8000\r\n" +
		"a=sendrecv\r\n"
	return []byte(body)
}

func userOf(s string) string {
	for i, c := range s {
		if c == '@' {
			return s[:i]
		}
	}
	return s
}
