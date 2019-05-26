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

test('uac session < 32s', {timeout: 45000}, (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios', maxBuffer: 100 * 102 * 11024});
      return;
    })
    .then(() => {
      return uac.call('sip:127.0.0.1:5095', {hangupAfter: 5000});
    })
    .then((emitter) => {
      t.pass('sent call')
      return new Promise((resolve) => {
        emitter.on('success', () => resolve());
      })
    })
    .then(() => {
      return t.pass('call connected, waiting 36s');
    }).then(() => {
      return new Promise((resolve) => {
        setTimeout(() => resolve(), 36000);
      })
    })
    .then(() => {
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

test('uac session > 32s', {timeout: 55000}, (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios', maxBuffer: 100 * 102 * 11024});
      return;
    })
    .then(() => {
      return uac.call('sip:127.0.0.1:5095', {hangupAfter: 10000});
    })
    .then((emitter) => {
      t.pass('sent call')
      return new Promise((resolve) => {
        emitter.on('success', () => resolve());
      })
    })
    .then(() => {
      return t.pass('call connected, waiting 50s for hangup');
    }).then(() => {
      return new Promise((resolve) => {
        setTimeout(() => resolve(), 50000);
      })
    })
    .then(() => {
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

test('uac invite/reinvite', {timeout: 55000}, (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success-reinvite.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios', maxBuffer: 100 * 102 * 11024});
      return;
    })
    .then(() => {
      return uac.call('sip:127.0.0.1:5095', {hangupAfter: 40000,  reinviteAfter: 5000});
    })
    .then((emitter) => {
      t.pass('sent call')
      return new Promise((resolve) => {
        emitter.on('success', () => resolve());
      })
    })
    .then(() => {
      return t.pass('call connected, waiting 50s for hangup');
    }).then(() => {
      return new Promise((resolve) => {
        setTimeout(() => resolve(), 50000);
      })
    })
    .then(() => {
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});
