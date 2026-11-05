const test = require('tape');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const {start, stop} = require('./testbed');

// Run standalone with: cd test && node notify-bye-tests.js
// The app waits until BYE destroys the dialog before answering the pending NOTIFY.
const run = (name, {unanswered = false, reinvite = false} = {}) => {
  test(name, async(t) => {
    let srf, pending, dialogEnded = false, received = false;
    const method = reinvite ? 'INVITE' : 'NOTIFY';
    try {
      await start(null, []);
      srf = new Srf();
      srf.connect(config.drachtio.connectOpts);
      await new Promise((resolve, reject) => {
        srf.on('connect', (err) => err ? reject(err) : resolve());
        srf.on('error', reject);
      });
      srf.invite(async(req, res) => {
        try {
          const dlg = await srf.createUAS(req, res, {localSdp: req.body});
          dlg.on(reinvite ? 'modify' : 'notify', (req, res) => {
            received = true;
            pending = res;
          });
          dlg.on('destroy', () => {
            dialogEnded = true;
            // Sending on the next turn lets BYE teardown complete in the server.
            if (!unanswered && !reinvite && pending) setTimeout(() => pending.send(200), 100);
          });
        } catch (err) { t.fail(err.message); }
      });
      const scenario = reinvite ? 'uac-pending-reinvite-bye' :
        unanswered ? 'uac-notify-bye-timeout' : 'uac-notify-bye';
      const expected = unanswered || reinvite ? 500 : 200;
      const began = Date.now();
      await execCmd(`sipp -sf ./${scenario}.xml 127.0.0.1:5090 -i 127.0.0.1 -t u1 -m 1 ` +
        `-timeout 40s -timeout_error`, {cwd: './scenarios'});
      t.ok(received, `application received the pending ${method}`);
      t.ok(dialogEnded, 'BYE destroyed the dialog before the pending request completed');
      t.pass(`SIPp received BYE 200 and ${method} ${expected}`);
      if (unanswered) t.ok(Date.now() - began >= 32000, 'unanswered NOTIFY survived until the cleanup deadline');
      if (reinvite) t.ok(Date.now() - began < 10000, 'pending re-INVITE was destroyed immediately');
    } catch (err) {
      t.fail(`failed with error ${err}`);
    } finally {
      if (srf) srf.disconnect();
      await stop();
      t.end();
    }
  });
};

run('pending NOTIFY accepts the application response after BYE');
run('unanswered NOTIFY expires after dialog teardown', {unanswered: true});
run('pending re-INVITE is destroyed with its dialog', {reinvite: true});
