const { spawn } = require('child_process');
const assert = require('assert');
const debug = require('debug')('drachtio:server-test');
const obj = module.exports = {} ;

let drachtio = null;
let router = null;

obj.start = (confPath) => {
  confPath = confPath || './drachtio.conf.xml';
  debug(`starting drachtio with config file: ${confPath}`);
  assert(!drachtio);

  return new Promise((resolve, reject) => {
    drachtio = spawn('../build/drachtio', ['-f', confPath], {
      detached: true,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    drachtio.on('error', (err) => {
      console.log(`Failed to start subprocess: ${err}`);
      reject(err);
    });

    drachtio.stdout.on('data', (data) => {
      debug(`${data.toString()}`);
    });
    drachtio.stderr.on('data', (data) => {
      debug(`${data.toString()}`);
    });

    router = spawn('node', ['./scripts/call-router'], {
      detached: true,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    router.on('error', (err) => {
      console.log(`failed to start call router: ${err}`);
      reject(err);
    });
    router.stdout.on('data', (data) => {
      debug(`${data.toString()}`);
    });
    router.stderr.on('data', (data) => {
      console.log(`${data.toString()}`);
    });

    setTimeout(() => {
      resolve(drachtio);
    }, 200);
  });
};

obj.stop = () => {
  assert(drachtio);
  assert(router);

  return new Promise((resolve, reject) => {
    debug('killing drachtio');
    drachtio.kill();
    debug('killing router');
    router.kill();
    drachtio = null;
    router = null;
    debug('waiting 200ms to end');
    setTimeout(() => {
      debug('resolving promise');
      resolve();
    }, 200);
  });
};
