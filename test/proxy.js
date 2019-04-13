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

test('proxy failed invite', (t) => {
  let uas;
  return start()
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('127.0.0.1:5091');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-fail-486.xml -i 127.0.0.1 -p 5091 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-expect-486.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('proxy 486 failiure succeeded');
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

test('proxy call with Record-Route', (t) => {
  let uas;
  return start()
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('127.0.0.1:5091');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5091 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('proxy succeeded');
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

test('proxy call - remove preloaded Route header', (t) => {
  let uas;
  return start()
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('127.0.0.1:5091');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5091 -m 1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-preloaded-route.xml 127.0.0.1:5090 -m 1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('Route header removed');
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

test('proxy call over tcp when transport=tcp appears in route', (t) => {
  let uas;
  return start()
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('sip:127.0.0.1:5091;transport=tcp');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5091 -m 1 -t t1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1 -t t1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('proxy succeeded');
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

test('attempt to proxy over tcp when the server has no udp tport', (t) => {
  let uas;
  return start('./drachtio.conf5.xml')
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('sip:127.0.0.1:5091');
    })
    .then(() => {
      execCmd('sipp -sf ./uas-success.xml -i 127.0.0.1 -p 5091 -m 1 -t t1', {cwd: './scenarios'});
      return;
    })
    .then(() => {
      return execCmd('sipp -sf ./uac.xml 127.0.0.1:5090 -m 1 -t t1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('proxy succeeded');
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

test('return 500 if call cannot be proxied due to lack of appropriate tport', (t) => {
  let uas;
  return start('./drachtio.conf6.xml')
    .then(() => {
      uas = new Uas();
      return uas.connect();
    })
    .then(() => {
      return uas.proxy('sip:127.0.0.1:5091;transport=udp');
    })
    .then(() => {
      return execCmd('sipp -sf ./uac-expect-500.xml 127.0.0.1:5090 -m 1 -t t1', {cwd: './scenarios'});
    })
    .then(() => {
      t.pass('proxy succeeded');
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