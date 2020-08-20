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

test('b2bua success', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.b2b('127.0.0.1:5093');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5093 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('b2b succeeded');
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

test('utf8 chars in Contact', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.b2b('127.0.0.1:5094');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5094 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-utf8.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('b2b succeeded');
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

test('b2bua multiple provisional responses', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.b2b('127.0.0.1:5095');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-183-180-200.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('b2b multiple provisionals success');
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

test('b2bua CANCEL', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
    uas = new Uas();
    return uas.connect();
  })
  .then(() => {
    return uas.b2b('127.0.0.1:5095');
  })
  .then(() => {
    execCmd('sipp -sf ./uas-cancel.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios'});
    return;
  })
  .then(() => {
    return execCmd('sipp -sf ./uac-cancel.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
  })
  .then(() => {
    t.pass('b2b cancel success');
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

test('b2bua ack-bye CANCEL race conition', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
    uas = new Uas();
    return uas.connect();
  })
  .then(() => {
    return uas.b2b('127.0.0.1:5095');
  })
  .then(() => {
    execCmd('sipp -sf ./uas-ackbye.xml -i 127.0.0.1 -p 5095 -m 1', {cwd: './scenarios'});
    return;
  })
  .then(() => {
    return execCmd('sipp -sf ./uac-cancel.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
  })
  .then(() => {
    t.pass('b2b ack-bye success');
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

test('b2bua timer H', (t) => {
  let uas;
  return start(null, ['--memory-debug'])
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.reject(480);
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-no-ack.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('b2b timerH success');
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


