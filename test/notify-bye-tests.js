// Regression for a pending incoming NOTIFY immediately followed by BYE.
// Run: cd test && node notify-bye-tests.js (uses ../build/drachtio).
// For an already running server, set DRACHTIO_ADMIN_PORT and DRACHTIO_SIP_{UDP,TCP,TLS}_PORT.
// SIP_NOTIFY_BYE_BASELINE=1 expects the original 500 and demonstrates the defect.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawn, execFileSync} = require("node:child_process");
const dgram = require("node:dgram");
const net = require("node:net");
const tls = require("node:tls");
const { randomUUID } = require("node:crypto");
const {setTimeout: delay} = require("node:timers/promises");
const Srf = require(process.env.DRACHTIO_SRF || "drachtio-srf");
const adminPort = Number(process.env.DRACHTIO_ADMIN_PORT || 9022);
const baseline = process.env.SIP_NOTIFY_BYE_BASELINE === "1";
const sdp = "v=0\r\no=smoke 1 1 IN IP4 127.0.0.1\r\ns=smoke\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 40000 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
function header(message, name) {
    return message.match(new RegExp(`^${name}:\\s*(.+)$`, "im"))?.[1]?.trim() ?? "";
}
async function createPeer(transport) {
    const port = Number(process.env[`DRACHTIO_SIP_${transport.toUpperCase()}_PORT`] || (transport === "tls" ? 5091 : 5090));
    const socket = transport === "udp" ? dgram.createSocket("udp4")
        : transport === "tls" ? tls.connect({ port, host: "127.0.0.1", rejectUnauthorized: false })
            : net.createConnection(port, "127.0.0.1");
    const callId = `${randomUUID()}@notify-bye-smoke`;
    const from = `<sip:avaya@localhost>;tag=${randomUUID()}`;
    let to = "<sip:service@localhost>";
    let sequence = 0;
    let stream = Buffer.alloc(0);
    const pending = new Map();
    const finals = new Map();
    const receive = (message) => {
        if (header(message, "Call-ID") !== callId || !/^SIP\/2.0 [2-6]/.test(message))
            return;
        const key = header(message, "CSeq");
        finals.set(key, [...(finals.get(key) ?? []), message]);
        const waiter = pending.get(key);
        if (waiter) {
            clearTimeout(waiter.timer);
            pending.delete(key);
            waiter.resolve(message);
        }
    };
    const fail = (error) => {
        for (const waiter of pending.values()) {
            clearTimeout(waiter.timer);
            waiter.reject(error);
        }
        pending.clear();
    };
    socket.on("error", fail);
    if (socket instanceof net.Socket) {
        socket.on("data", (chunk) => {
            stream = Buffer.concat([stream, chunk]);
            for (;;) {
                const end = stream.indexOf("\r\n\r\n");
                if (end < 0)
                    break;
                const length = end + 4 + Number(header(stream.subarray(0, end).toString(), "Content-Length") || 0);
                if (stream.length < length)
                    break;
                receive(stream.subarray(0, length).toString());
                stream = stream.subarray(length);
            }
        });
        await new Promise((resolve, reject) => {
            socket.once(transport === "tls" ? "secureConnect" : "connect", resolve);
            socket.once("error", reject);
        });
    }
    else {
        socket.on("message", message => receive(message.toString()));
        await new Promise(resolve => socket.bind(0, "0.0.0.0", resolve));
    }
    const localPort = socket instanceof net.Socket ? socket.localPort : socket.address().port;
    const send = (message) => {
        if (socket instanceof net.Socket)
            socket.write(message);
        else
            socket.send(message, port, "127.0.0.1");
    };
    const message = (method, cseq, headers, body = "") => [
        `${method} sip:service@127.0.0.1:${port} SIP/2.0`,
        `Via: SIP/2.0/${transport.toUpperCase()} 127.0.0.1:${localPort};rport;branch=z9hG4bK-${randomUUID()}`,
        "Max-Forwards: 70", `From: ${from}`, `To: ${to}`, `Call-ID: ${callId}`, `CSeq: ${cseq} ${method}`,
        `Contact: <sip:avaya@127.0.0.1:${localPort};transport=${transport}>`, ...headers,
        `Content-Length: ${Buffer.byteLength(body)}`, "", body,
    ].join("\r\n");
    const request = (method, headers = [], body = "", timeoutMs = 5000) => {
        const cseq = ++sequence;
        const key = `${cseq} ${method}`;
        const response = new Promise((resolve, reject) => {
            const timer = setTimeout(() => { pending.delete(key); reject(new Error(`${transport} ${callId}: timeout for ${key}`)); }, timeoutMs);
            pending.set(key, { resolve, reject, timer });
        });
        send(message(method, cseq, headers, body));
        return { key, response };
    };
    return {
        request,
        async establish() {
            const response = await request("INVITE", ["Content-Type: application/sdp"], sdp).response;
            assert.match(response, /^SIP\/2.0 200 /);
            to = header(response, "To");
            send(message("ACK", sequence, []));
            // Give the createUAS continuation time to install its event listeners.
            await delay(30);
        },
        close() {
            fail(new Error("peer closed"));
            if (socket instanceof net.Socket)
                socket.destroy();
            else
                socket.close();
        },
        assertSingleFinal(key) {
            assert.equal(finals.get(key)?.length, 1, `${transport}: exactly one final response for ${key}`);
        },
    };
}
const notifyHeaders = ["Event: refer", "Subscription-State: terminated;reason=noresource", "Content-Type: message/sipfrag"];
async function checkRace(transport, status = 200, method = "NOTIFY", count = 1) {
    const peer = await createPeer(transport);
    try {
        await peer.establish();
        // The application deliberately replies after BYE has already torn down the dialog.
        const notifications = Array.from({ length: count }, () => peer.request(method, [...notifyHeaders, `X-Smoke-Status: ${status}`], "SIP/2.0 200 OK\r\n"));
        const bye = peer.request("BYE");
        const [notifyResponses, byeResponse] = await Promise.all([Promise.all(notifications.map(request => request.response)), bye.response]);
        const expected = baseline ? 500 : status;
        for (const response of notifyResponses) {
            assert.match(response, new RegExp(`^SIP/2.0 ${expected} `), `${transport}: delayed ${method} must retain its application response after BYE`);
        }
        assert.match(byeResponse, /^SIP\/2.0 200 /);
        await delay(150);
        for (const request of notifications)
            peer.assertSingleFinal(request.key);
        peer.assertSingleFinal(bye.key);
        if (!baseline) {
            // The transaction must survive, but the ended INVITE dialog must not.
            const late = await peer.request("NOTIFY", notifyHeaders, "SIP/2.0 200 OK\r\n").response;
            // Drachtio returns 503 when no out-of-dialog NOTIFY app is registered;
            // stacks that reject the missing dialog directly return 481 instead.
            assert.match(late, /^SIP\/2.0 (481|503) /, "new requests after BYE must still be rejected");
        }
        console.log(`${baseline ? "REPRODUCED" : "PASS"}: ${transport} ${count} pending ${method}/BYE, request ${expected}, BYE 200`);
    }
    finally {
        peer.close();
    }
}
async function checkNormal(transport) {
    const peer = await createPeer(transport);
    try {
        await peer.establish();
        const progress = await peer.request("NOTIFY", ["Event: refer", "Subscription-State: active", "Content-Type: message/sipfrag", "X-Smoke-Status: 200"], "SIP/2.0 183 Session Progress\r\n").response;
        assert.match(progress, /^SIP\/2.0 200 /);
        const final = await peer.request("NOTIFY", [...notifyHeaders, "X-Smoke-Status: 200"], "SIP/2.0 200 OK\r\n").response;
        assert.match(final, /^SIP\/2.0 200 /);
        assert.match(await peer.request("BYE").response, /^SIP\/2.0 200 /);
        console.log(`PASS: ${transport} normal NOTIFY responses before BYE`);
    }
    finally {
        peer.close();
    }
}
async function checkUnanswered(transport) {
    const peer = await createPeer(transport);
    try {
        await peer.establish();
        const started = performance.now();
        const notification = peer.request("NOTIFY", [...notifyHeaders, "X-Smoke-Status: never"], "SIP/2.0 200 OK\r\n", 38000);
        const reply = notification.response.then(message => ({ message, elapsed: performance.now() - started }));
        assert.match(await peer.request("BYE").response, /^SIP\/2.0 200 /);
        const { message, elapsed } = await reply;
        assert.match(message, /^SIP\/2.0 500 /);
        assert.ok(elapsed >= 31000 && elapsed < 38000, `unanswered request must expire after the 32s grace period, got ${elapsed}ms`);
        peer.assertSingleFinal(notification.key);
        console.log(`PASS: ${transport} unanswered NOTIFY expired after ${Math.round(elapsed)}ms`);
    }
    finally {
        peer.close();
    }
}
async function main() {
    for (let attempt = 0;; attempt++) {
        try {
            await new Promise((resolve, reject) => {
                const socket = net.createConnection(adminPort, "127.0.0.1");
                socket.once("connect", () => { socket.destroy(); resolve(); });
                socket.once("error", reject);
            });
            break;
        }
        catch (error) {
            if (attempt >= 49)
                throw error;
            await delay(100);
        }
    }
    const srf = new Srf();
    const applicationErrors = [];
    const replies = [];
    srf.invite(async (req, res) => {
        try {
            const dialog = await srf.createUAS(req, res, { localSdp: sdp });
            const respond = (request, response) => {
                if (request.get("X-Smoke-Status") === "never")
                    return;
                const reply = delay(100).then(() => new Promise((resolve, reject) => {
                    response.send(Number(request.get("X-Smoke-Status") || 200), (error) => error ? reject(error) : resolve());
                })).catch(error => { if (!baseline)
                    applicationErrors.push(error); });
                replies.push(reply);
            };
            dialog.on("notify", respond);
            dialog.on("info", respond);
        }
        catch (error) {
            applicationErrors.push(error);
        }
    });
    try {
        await new Promise((resolve, reject) => {
            srf.once("connect", resolve);
            srf.once("error", reject);
            srf.connect({ host: "127.0.0.1", port: adminPort, secret: "smoke" });
        });
        for (const transport of ["udp", "tcp", "tls"]) {
            for (let attempt = 0; attempt < 3; attempt++)
                await checkRace(transport);
            if (!baseline) {
                await checkRace(transport, 489);
                await checkRace(transport, 200, "INFO");
                await checkRace(transport, 200, "NOTIFY", 3);
                await checkNormal(transport);
            }
        }
        if (!baseline) {
            await Promise.all([checkUnanswered("udp"), checkUnanswered("tcp"), checkUnanswered("tls")]);
            // Completed requests' cleanup timers have also fired; the process must remain healthy.
            await checkNormal("tcp");
        }
        await Promise.all(replies);
        assert.deepEqual(applicationErrors, [], "Drachtio must accept the application's delayed responses without unknown-transaction errors");
    }
    finally {
        srf.disconnect();
    }
}

(async () => {
    let server, directory, logPath, startupError;
    try {
        if (!process.env.DRACHTIO_ADMIN_PORT) {
            directory = fs.mkdtempSync(path.join(os.tmpdir(), "drachtio-notify-bye-"));
            const key = path.join(directory, "tls.key");
            const cert = path.join(directory, "tls.crt");
            execFileSync("openssl", ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-subj", "/CN=localhost", "-keyout", key, "-out", cert], {stdio: "ignore"});
            const configPath = path.join(directory, "drachtio.conf.xml");
            fs.writeFileSync(configPath, `<drachtio>
  <admin port="9022" secret="smoke">127.0.0.1</admin>
  <sip>
    <contacts>
      <contact>sip:127.0.0.1:5090;transport=udp,tcp</contact>
      <contact>sips:127.0.0.1:5091;transport=tls</contact>
    </contacts>
    <tls><min-tls-version>1.2</min-tls-version><key-file>${key}</key-file>
      <cert-file>${cert}</cert-file><chain-file>${cert}</chain-file></tls>
  </sip>
  <cdrs>false</cdrs>
  <logging><console/><sofia-loglevel>0</sofia-loglevel><loglevel>info</loglevel></logging>
</drachtio>`);
            logPath = path.join(directory, "drachtio.log");
            const output = fs.openSync(logPath, "w");
            try {
                server = spawn(process.env.DRACHTIO_SERVER || path.join(__dirname, "../build/drachtio"),
                    ["-f", configPath], {stdio: ["ignore", output, output]});
                server.on("error", error => { startupError = error; });
            } finally { fs.closeSync(output); }
        }
        await main();
    } catch (error) {
        console.error(startupError || error);
        if (logPath) console.error(fs.readFileSync(logPath, "utf8"));
        process.exitCode = 1;
    } finally {
        if (server && server.pid && server.exitCode === null && server.signalCode === null) {
            await new Promise(resolve => {
                const timeout = setTimeout(() => server.kill("SIGKILL"), 2000);
                server.once("exit", () => { clearTimeout(timeout); resolve(); });
                server.kill("SIGTERM");
            });
        }
        if (directory) fs.rmSync(directory, {recursive: true, force: true});
    }
})();
