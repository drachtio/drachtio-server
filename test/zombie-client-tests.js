const test = require('blue-tape');
const {start, stop} = require('./testbed');
const {exec} = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const Uas = require('./scripts/uas');
const WedgeClient = require('./scripts/wedge-client');
const delay = require('./utils/delay');
const debug = require('debug')('drachtio:server-test');

const execCmd = (cmd, opts) => {
  opts = opts || {};
  return new Promise((resolve, reject) => {
    setTimeout(() => {
      exec(cmd, opts, (err, stdout, stderr) => {
        if (stdout) debug(stdout);
        if (stderr) debug(stderr);
        if (err) return reject(err);
        resolve();
      });
    }, 750);
  });
};

/* poll a log file until it matches, or give up after timeoutMs */
const waitForLogMatch = (logPath, re, timeoutMs) => {
  const startAt = Date.now();
  return new Promise((resolve) => {
    const check = () => {
      let content = '';
      try {
        content = fs.readFileSync(logPath, {encoding: 'utf8'});
      } catch (e) { /* not written yet */ }
      if (re.test(content)) return resolve(true);
      if (Date.now() - startAt > timeoutMs) return resolve(false);
      setTimeout(check, 500);
    };
    check();
  });
};

/**
 * Regression test for the zombie-client outage: a client whose socket
 * writes never complete (it stopped reading, so its tcp window closed)
 * must be evicted from the request-dispatch pool rather than left in
 * round-robin rotation black-holing every third INVITE.
 */
test('wedged client is evicted and INVITEs only route to healthy clients', async(t) => {
  const logPath = path.join(os.tmpdir(), `drachtio-wedge-test-${process.pid}.log`);
  let uas1, uas2, wedge;
  try {
    /* short write timeout so the test doesn't dawdle */
    await start(null, [], false, 1000, {DRACHTIO_CLIENT_WRITE_TIMEOUT: '4'}, logPath);

    uas1 = new Uas();
    uas2 = new Uas();
    await uas1.connect();
    uas1.accept();
    await uas2.connect();
    uas2.accept();

    wedge = new WedgeClient();
    await wedge.connect();
    await wedge.route('invite');

    /* stop reading, then make the server write to us until its sends jam;
       in production this was app traffic to a hung client, here pings do it.
       the volume must exceed what the kernels will buffer between the two
       sockets (with autotuning, several MB): 100k pings draw ~9MB of
       responses, comfortably past it */
    wedge.wedge();
    wedge.flood(100000);

    const evicted = await waitForLogMatch(logPath, /evicting unresponsive client/, 15000);
    t.ok(evicted, 'server evicted the wedged client after writes stopped completing');

    for (let i = 0; i < 6; i++) {
      await execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    }
    t.equal(uas1.calls + uas2.calls, 6, 'all 6 INVITEs were answered by the healthy clients');
    t.ok(uas1.calls >= 2 && uas2.calls >= 2, 'both healthy clients took calls');
  } finally {
    if (wedge) wedge.disconnect();
    if (uas1) uas1.disconnect();
    if (uas2) uas2.disconnect();
    await stop();
  }
});

/**
 * Regression test for remove_route: unregistering one verb must not drop
 * the client's other registrations (a draining instance stops taking new
 * INVITEs but must keep answering OPTIONS health checks).
 */
test('unregistering one verb leaves the client\'s other routes intact', async(t) => {
  let uasA, uasB;
  let optionsReceived = 0;
  try {
    await start();

    uasA = new Uas();
    await uasA.connect();
    uasA.srf.options((req, res) => {
      optionsReceived++;
      res.send(200);
    });
    uasA.accept();

    uasB = new Uas();
    await uasB.connect();
    uasB.accept();
    await delay(500);

    uasA.srf.unregisterForMessages('invite');
    await delay(500);

    /* OPTIONS from a non-auto-answered user-agent must still reach uasA */
    await execCmd('sipp -sf ./uac-options-routed-expect-200.xml 127.0.0.1:5090 -m 1 -timeout 10s -timeout_error',
      {cwd: './scenarios'});
    t.equal(optionsReceived, 1, 'client still receives OPTIONS after unregistering only invite');

    await execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    await execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    t.equal(uasB.calls, 2, 'all INVITEs went to the client still registered for invite');
    t.equal(uasA.calls, 0, 'no INVITEs went to the client that unregistered invite');
  } finally {
    if (uasA) uasA.disconnect();
    if (uasB) uasB.disconnect();
    await stop();
  }
});

test('finish up', (t) => {
  t.end();
  process.exit(0);
});
