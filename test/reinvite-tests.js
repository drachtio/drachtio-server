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
