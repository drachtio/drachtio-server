const test = require('blue-tape');
const {start, stop } = require('./testbed');
const { exec } = require('child_process');
const Uac = require('./scripts/uac');
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

test('requested protocol not available', (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--contact', 'sip:127.0.0.1;transport=udp'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      return uac.options('sip:192.168.100.53;transport=tcp');
    })
    .catch((err) => {
      t.equal(err, 'requested protocol/transport not available', 'fails with expected error');
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uac.disconnect();
          resolve();
        }, 1000);
      });
    })
    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

