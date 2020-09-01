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
  return start('./drachtio.conf4.xml', ['--memory-debug'], false, 500, {
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
  return start('./drachtio.conf4.xml', ['--memory-debug'])
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
/*
test('handle uas sending BYE instead of final response', (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-bad-bye.xml -i 127.0.0.1 -p 5091 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return uac.invite('sip:127.0.0.1:5091');
    })
    .then((req) => {
      return new Promise((resolve) => {
        req.on('response', (res) => {
          debug(`got response ${res.status} waiting..`);
          setTimeout(() => resolve(), 300000);
        });
      })
    })
    .then(() => {
      t.pass('successfully handled delayed response');
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});
*/
test('retransmit OPTIONS', (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-options-delay.xml -i 127.0.0.1 -p 5091 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return uac.options('sip:127.0.0.1:5091');
    })
    .then((req) => {
      return new Promise((resolve) => {
        req.on('response', () => resolve());
      })
    })
    .then(() => {
      t.pass('successfully handled delayed response');
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

test('retransmit INVITE', (t) => {
  let uac;
  return start('./drachtio.conf4.xml', ['--memory-debug'])
    .then(() => {
      uac = new Uac();
      return uac.connect();
    })
    .then(() => {
      execCmd('sipp -sf ./uas-invite-delay.xml -i 127.0.0.1 -p 5092 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return uac.invite('sip:127.0.0.1:5092');
    })
    .then((req) => {
      return new Promise((resolve) => {
        req.on('response', () => resolve());
      })
    })
    .then(() => {
      t.pass('successfully handled delayed response');
      uac.disconnect();
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uac) uac.disconnect();
      stop();
    });
});

