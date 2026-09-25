const test = require('tape');
const fs = require('fs');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const {start, stop} = require('./testbed');

// Requests we send inside a dialog must be released once their final response arrives,
// and a final response that follows a provisional (e.g. 180/183 to a re-INVITE) must
// still reach the app.  Each case runs one sipp call against a fresh drachtio and checks
// the RIP (request-in-progress) count in the storage dump drachtio writes on SIGTERM.

// the file sink configured in drachtio.conf.xml
const LOG = '/tmp/drachtio.log';
const MESSAGES = 200;

const connect = () => {
  const srf = new Srf();
  srf.connect(config.drachtio.connectOpts);
  return new Promise((resolve, reject) => {
    srf.on('connect', (err) => err ? reject(err) : resolve(srf));
    srf.on('error', () => {});
  });
};

const answer = (srf) => new Promise((resolve, reject) => {
  srf.invite((req, res) => {
    srf.createUAS(req, res, {localSdp: req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000')})
      .then(resolve, reject);
  });
});

const newSdp = (dlg) => dlg.local.sdp.replace(/m=audio\s+(\d+)/, 'm=audio 15002');

// re-INVITE sent the way an app has to when the far end may answer with a reliable
// provisional: Dialog#modify does not send PRACK, so drive the request ourselves
const reinviteWithPrack = (dlg) => new Promise((resolve, reject) => {
  const statuses = [];
  dlg.agent.request({
    method: 'INVITE',
    stackDialogId: dlg.id,
    body: newSdp(dlg),
    _socket: dlg.socket,
    headers: {Contact: dlg.local.contact}
  }, (err, req) => {
    if (err) return reject(err);
    req.on('response', (res, ack) => {
      statuses.push(res.status);
      if (res.status < 200) {
        if (res.has('RSeq')) ack();
        return;
      }
      ack();
      resolve(statuses);
    });
  });
});

const withTimeout = (p, ms) => Promise.race([
  p, new Promise((_, reject) => setTimeout(() => reject(new Error(`timed out after ${ms}ms`)), ms))
]);

const lastCount = (log, label) => {
  const re = new RegExp(`${label}\\s+(\\d+)`, 'g');
  let m, v;
  while ((m = re.exec(log))) v = parseInt(m[1]);
  return v;
};

const run = (name, scenario, inCall) => {
  if (process.env.ONLY && !name.includes(process.env.ONLY)) return;
  test(name, async(t) => {
    let srf, sippP;
    try { fs.unlinkSync(LOG); } catch (e) { /* not there yet */ }
    try {
      await start(null, []);
      srf = await connect();
      const dlgP = answer(srf);
      sippP = execCmd(`sipp -sf ./${scenario}.xml 127.0.0.1:5090 -m 1 -timeout 20s -timeout_error`,
        {cwd: './scenarios'});
      sippP.catch(() => {}); // awaited below; don't let an early sipp failure crash the run
      const dlg = await dlgP;
      await withTimeout(inCall(t, dlg), 10000);
      await dlg.destroy();
      await sippP;
      t.pass('sipp scenario completed');
    } catch (err) {
      t.fail(`failed with error ${err}`);
      // let sipp time out, so it frees its port for the next case
      if (sippP) await sippP.catch(() => {});
    }
    if (srf) srf.disconnect();
    await stop();
    const log = fs.readFileSync(LOG, 'utf8');
    t.equal(lastCount(log, 'RIP size:'), 0, 'no RIPs left after the call');
    t.equal(lastCount(log, 'IIP size:'), 0, 'no IIPs left after the call');
    t.end();
  });
};

run('re-INVITE answered 100, 180, 200 completes', 'uac-recv-reinvite-180-200', async(t, dlg) => {
  const sdp = await dlg.modify(newSdp(dlg));
  t.ok(sdp, 'app gets the 200 OK after the 180');
});

run('re-INVITE answered 100, 183 with SDP, 200 completes', 'uac-recv-reinvite-183-sdp-200', async(t, dlg) => {
  const sdp = await dlg.modify(newSdp(dlg));
  t.ok(sdp, 'app gets the 200 OK after the 183');
});

run('re-INVITE answered 100, 183, 488 fails with 488', 'uac-recv-reinvite-183-488', async(t, dlg) => {
  try {
    await dlg.modify(newSdp(dlg));
    t.fail('re-INVITE should fail');
  } catch (err) {
    t.equal(err.status, 488, 'app gets the 488 after the 183');
  }
});

run('re-INVITE answered 100, reliable 183, PRACK, 200 completes', 'uac-recv-reinvite-183-rel-prack-200', async(t, dlg) => {
  const statuses = await reinviteWithPrack(dlg);
  t.equal(statuses[statuses.length - 1], 200, `app gets the 200 OK after the PRACK (${statuses.join(', ')})`);
});

run(`${MESSAGES} in-dialog MESSAGEs are released`, 'uac-recv-messages', async(t, dlg) => {
  for (let i = 0; i < MESSAGES; i++) {
    await dlg.request({method: 'MESSAGE', headers: {'Content-Type': 'text/plain'}, body: `${i}`});
  }
  t.pass(`sent ${MESSAGES} MESSAGEs`);
});
