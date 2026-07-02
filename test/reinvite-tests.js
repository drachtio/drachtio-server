const test = require('blue-tape');
const {start, stop } = require('./testbed');
const { exec } = require('child_process');
const Uas = require('./scripts/uas');
const debug = require('debug')('drachtio:server-test');

const execCmd = (cmd, opts) => {
  opts = opts || {} ;
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

test('uas gets reinvite with null sdp', (t) => {
  let uas, p;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      p = uas.handleReinviteScenario();
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-send-reinvite-no-sdp.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return p;
    })
    .then(() => {
      t.pass('reinvite succeeded and fnAck(ack) called');
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas.disconnect();
          resolve();
        }, 1000);
      });
    })
    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas) uas.disconnect();
      stop();
    });
});

test('uas sends BYE if session expires without expected UAC refresh', (t) => {
  let uas, p;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      p = uas.handleSessionExpired();
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-session-expires.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return p;
    })
    .then(() => {
      t.pass('generate BYE if uac does not refresh as agreed');
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas.disconnect();
          resolve();
        }, 1000);
      });
    })
    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas) uas.disconnect();
      stop();
    });
});

test('uas honors default-refresher=uac config and BYEs when Session-Expires offered without a refresher', (t) => {
  let uas, p;
  // Start drachtio with the opt-in config (default-refresher="uac"), so a
  // refresher-less Session-Expires offer causes drachtio to nominate the UAC
  // as refresher and BYE the call when it never refreshes.
  return start('./drachtio.session-timer-uac.conf.xml', ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      p = uas.handleSessionExpired();
      return;
    })
    .then(() => {
      // The sipp scenario asserts the 200 OK contains "refresher=uac" via an
      // <ereg check_it="true"> action — sipp will exit non-zero (rejecting the
      // execCmd promise) if drachtio omits the parameter. It then deliberately
      // skips refreshing so drachtio must BYE when the session interval expires.
      return execCmd('sipp -sf ./uac-session-expires-no-refresher.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return p;
    })
    .then(() => {
      t.pass('200 OK included refresher=uac and drachtio BYEd on session expiry');
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas.disconnect();
          resolve();
        }, 1000);
      });
    })
    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas) uas.disconnect();
      stop();
    });
});

test('uas does not arm a session timer when Session-Expires offered without a refresher (default none)', (t) => {
  const fs = require('fs');
  let uas;
  // Default config resolves default-refresher to "none": for a refresher-less
  // Session-Expires offer drachtio must decline the session timer (no timer
  // armed, no teardown). We capture drachtio's log and assert on its decision,
  // rather than trying to assert the absence of a header in sipp (which this
  // sipp version can't express, and which otherwise races the interval).
  const logPath = '/tmp/drachtio-none-test.log';
  try { fs.unlinkSync(logPath); } catch (e) { /* not there yet */ }

  return start(null, ['--memory-debug'], false, 500, {}, logPath)
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      uas.handleReinvite();
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-session-expires-no-refresher-default.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      // brief settle so the console sink flushes to the log file
      return new Promise((resolve) => setTimeout(resolve, 300));
    })
    .then(() => {
      const log = fs.readFileSync(logPath, 'utf8');
      t.ok(log.includes("default-refresher is 'none'; not arming a session timer"),
        'drachtio should decline to arm a session timer for the refresher-less offer');
      t.notOk(/Session timer expired/.test(log),
        'drachtio should not tear the call down with a session-timer BYE');
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas.disconnect();
          resolve();
        }, 1000);
      });
    })
    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas) uas.disconnect();
      stop();
    });
});
