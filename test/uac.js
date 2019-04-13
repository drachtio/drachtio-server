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
test('reads config from ENV', (t) => {
  let uac;
  return start('./drachtio.conf4.xml', [], false, 500, {
    'DRACHTIO_SECRET': 'foobar'
  })
    .then(() => {
      uac = new Uac();
      return uac.connect({
        "host": "127.0.0.1",
        "port": 9022,
        "secret": "foobar"
      });
    })
    .then(() => {
      t.pass('successfully used environment variable');
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

test('requested protocol not available', (t) => {
  let uac;
  return start('./drachtio.conf4.xml')
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

