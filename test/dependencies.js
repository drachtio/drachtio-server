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

test('verify dependencies', (t) => {
  return execCmd('sipp -v')
    .catch((err) => {
      t.ok(err.code === 99, 'sipp is installed');
      return execCmd('node --version');
    })
    .then(() => {
      return t.pass('node is installed');
    })
    .catch((err) => {
      t.fail('node is not installed');
    });
});
