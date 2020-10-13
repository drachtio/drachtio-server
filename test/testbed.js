const { spawn } = require('child_process');
const assert = require('assert');
const debug = require('debug')('drachtio:server-test');
const obj = module.exports = {} ;

let drachtio = null;
let router = null;

obj.start = (confPath, extraArgs, tls = false, waitDelay = 500, env = {}) => {
  confPath = confPath || './drachtio.conf.xml';
  debug(`starting drachtio with config file: ${confPath} and env ${JSON.stringify(env)}`);
  assert(!drachtio);
  assert(!router);

  return new Promise((resolve, reject) => {
    const args = ['-f', confPath].concat(Array.isArray(extraArgs) ? extraArgs : []);

    drachtio = spawn('../build/drachtio', args, {
      env,
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

    const routerArgs = ['./scripts/call-router'];
    if (tls) routerArgs.push(['--transport=tls']);
    router = spawn('node', routerArgs, {
      detached: true,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    router.on('error', (err) => {
      console.log(`failed to start call router: ${err}`);
      reject(err);
    });
    router.stdout.on('data', (data) => {
      console.log(`${data.toString()}`);
    });
    router.stderr.on('data', (data) => {
      console.log(`${data.toString()}`);
    });

    setTimeout(() => {
      resolve(drachtio);
    }, waitDelay);
  });
};

obj.stop = () => {
  if (!process.env.NOSERVER) assert(drachtio);
  assert(router);

  return new Promise((resolve, reject) => {
    debug('killing drachtio');
    drachtio.kill('SIGTERM');
    debug('killing router');
    router.kill();
    router = null;
    //drachtio = null;
    drachtio.on('exit', () => {
      drachtio = null;
      debug('drachtio exited');
      resolve();
    });
  });
};
