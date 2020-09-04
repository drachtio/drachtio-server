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

test('simple UAS', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.answer();
    })
    .then(() => {
      return execCmd('sipp -sf uac.xml localhost:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('uas succeeded');
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

