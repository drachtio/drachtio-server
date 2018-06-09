const test = require('blue-tape');
const {start, stop } = require('./testbed');
const { exec } = require('child_process');
const Uas = require('./scripts/uas');
//const Uac = require('./scripts/uac');
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

test('no apps running', (t) => {
  return start()
    .then(() => {
      return execCmd('sipp -sf ./uac-expect-503.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return t.pass('sends 503 when no apps connected');
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-spammer-expect-603.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return t.pass('send 603 when spammer detected');
    })
    .then(stop);
});

test('round robin calls between apps', (t) => {
  let uas1, uas2, uas3;
  return start()
    .then(() => {
      uas1 = new Uas();
      uas2 = new Uas();
      uas3 = new Uas();
      return Promise.all([uas1.connect(), uas2.connect(), uas3.connect()]);
    })
    .then(() => {
      return Promise.all([uas1.accept(), uas2.accept(), uas3.accept()]);
    })
    .then(() => {
      t.pass('sending 4 calls to 3 apps (all with inbound connections)');
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.ok(uas1.calls === 2, 'uas1 got 2 calls');
      t.ok(uas2.calls === 1, 'uas2 got 1 call');
      t.ok(uas3.calls === 1, 'uas3 got 1 call');
      return t.pass('round robin successful');
    })
    .then(() => {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas1.disconnect();
          uas2.disconnect();
          uas3.disconnect();
          resolve();
        }, 1000);
      });
    })

    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas1) uas1.disconnect();
      if (uas2) uas2.disconnect();
      if (uas3) uas3.disconnect();
      stop();
    });
});

test  ('skips over and removes disconnected clients', (t) => {
  let uas1, uas2, uas3;
  return start()
    .then(() => {
      uas1 = new Uas();
      uas2 = new Uas();
      uas3 = new Uas();
      return Promise.all([uas1.connect(), uas2.connect(), uas3.connect()]);
    })
    .then(() => {
      return Promise.all([uas1.accept(), uas2.accept(), uas3.accept()]);
    })
    .then(() => {
      t.pass('started 3 apps (all inbound connections)');
      t.pass('now disconnecting the app #2');
      uas2.disconnect();
      uas2 = null;
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.ok(uas1.calls === 2, 'uas1 got 2 calls');
      t.ok(uas3.calls === 1, 'uas3 got 1 calls');
      return t.pass('skipped over and removed disconnected client');
    })
    .then(() => {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas1.disconnect();
          uas3.disconnect();
          resolve();
        }, 1000);
      });
    })

    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas1) uas1.disconnect();
      if (uas2) uas2.disconnect();
      if (uas3) uas3.disconnect();
      stop();
    });
});

test('tag tests', (t) => {
  let uas1, uas2, uas3;
  return start('./drachtio.conf2.xml')
    .then(() => {
      uas1 = new Uas('orange');
      uas2 = new Uas('red');
      uas3 = new Uas(['blue', 'green']);
      return Promise.all([uas1.connect(), uas2.connect(), uas3.connect()]);
    })
    .then(() => {
      return Promise.all([uas1.accept(), uas2.accept(), uas3.accept()]);
    })
    .then(() => {
      return execCmd('sipp -sf ./uac_blue.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('blue call went to blue app');
      return execCmd('sipp -sf ./uac_green.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('green call went to green app');
      return execCmd('sipp -sf ./uac_red.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('red call went to red app');
      return execCmd('sipp -sf ./uac_orange.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('orange call went to orange app');
      return execCmd('sipp -sf ./uac_black.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('480 returned if there is no app to support a given tag');
      t.ok(uas1.calls === 1, 'uas1 got 1 call');
      t.ok(uas2.calls === 1, 'uas2 got 1 call');
      t.ok(uas3.calls === 2, 'uas3 got 2 call');
      return;
    })
    .then(() => {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          uas1.disconnect();
          uas2.disconnect();
          uas3.disconnect();
          resolve();
        }, 1000);
      });
    })

    .then(() => {
      return stop();
    })
    .catch((err) => {
      t.fail(`failed with error ${err}`);
      if (uas1) uas1.disconnect();
      if (uas2) uas2.disconnect();
      if (uas3) uas3.disconnect();
      stop();
    });
});

