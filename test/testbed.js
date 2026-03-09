const { spawn, execSync } = require('child_process');
const debug = require('debug')('drachtio:server-test');
const obj = module.exports = {} ;

let drachtio = null;
let router = null;

function killStaleProcesses() {
  try {
    // Kill any leftover drachtio processes from previous test runs
    const pids = execSync('pgrep -f "build/drachtio.*-f"', {encoding: 'utf8'}).trim().split('\n');
    for (const pid of pids) {
      if (pid) {
        debug(`killing stale drachtio process ${pid}`);
        try { process.kill(parseInt(pid), 'SIGKILL'); } catch (e) { /* already dead */ }
      }
    }
  } catch (e) { /* no matching processes */ }

  try {
    // Kill any leftover call-router processes
    const pids = execSync('pgrep -f "node.*call-router"', {encoding: 'utf8'}).trim().split('\n');
    for (const pid of pids) {
      if (pid) {
        debug(`killing stale call-router process ${pid}`);
        try { process.kill(parseInt(pid), 'SIGKILL'); } catch (e) { /* already dead */ }
      }
    }
  } catch (e) { /* no matching processes */ }
}

function waitForPort(port, timeoutMs = 10000) {
  const start = Date.now();
  let killed = false;
  return new Promise((resolve, reject) => {
    const check = () => {
      try {
        const output = execSync(`lsof -i :${port} -P -t`, {encoding: 'utf8'}).trim();
        // Port still in use — check if it's a sipp process we can kill
        if (!killed) {
          const pids = output.split('\n').filter(Boolean);
          for (const pid of pids) {
            try {
              const cmd = execSync(`ps -p ${pid} -o comm=`, {encoding: 'utf8'}).trim();
              if (cmd.includes('sipp')) {
                debug(`killing stale sipp process ${pid} on port ${port}`);
                process.kill(parseInt(pid), 'SIGKILL');
                killed = true;
              }
            } catch (e) { /* process already gone */ }
          }
        }
        if (Date.now() - start > timeoutMs) {
          reject(new Error(`port ${port} still in use after ${timeoutMs}ms`));
        } else {
          setTimeout(check, 250);
        }
      } catch (e) {
        // lsof returns non-zero when no process found — port is free
        resolve();
      }
    };
    check();
  });
}

function forceKill(proc) {
  if (!proc) return;
  try { proc.kill('SIGTERM'); } catch (e) { /* already dead */ }
  setTimeout(() => {
    try { proc.kill('SIGKILL'); } catch (e) { /* already dead */ }
  }, 2000);
}

// Clean up on unexpected exit
function cleanup() {
  forceKill(drachtio);
  forceKill(router);
  drachtio = null;
  router = null;
}

process.on('exit', cleanup);
process.on('SIGINT', () => { cleanup(); process.exit(1); });
process.on('SIGTERM', () => { cleanup(); process.exit(1); });

obj.waitForPort = waitForPort;

obj.start = async (confPath, extraArgs, tls = false, waitDelay = 500, env = {}) => {
  confPath = confPath || './drachtio.conf.xml';
  debug(`starting drachtio with config file: ${confPath} and env ${JSON.stringify(env)}`);

  // Kill any stale processes before first start
  if (!drachtio && !router) {
    killStaleProcesses();
    // Wait for admin port to be free
    await waitForPort(9022, 5000).catch(() => {
      debug('warning: port 9022 still in use, proceeding anyway');
    });
  }

  if (drachtio || router) {
    debug('warning: start called with existing processes, cleaning up first');
    await obj.stop().catch(() => {});
  }

  return new Promise((resolve, reject) => {
    const args = ['-f', confPath].concat(Array.isArray(extraArgs) ? extraArgs : []);

    drachtio = spawn('../build/drachtio', args, {
      env,
      detached: true,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    drachtio.on('error', (err) => {
      console.log(`Failed to start subprocess: ${err}`);
      drachtio = null;
      reject(err);
    });
    drachtio.on('exit', (code, signal) => {
      debug(`drachtio exited with code ${code}, signal ${signal}`);
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
  return new Promise((resolve) => {
    if (!drachtio && !router) {
      debug('stop called but nothing to stop');
      return resolve();
    }

    let resolved = false;
    const done = () => {
      if (!resolved) {
        resolved = true;
        resolve();
      }
    };

    // Safety timeout — don't hang forever waiting for exit
    const timer = setTimeout(() => {
      debug('stop: timed out waiting for drachtio to exit, force killing');
      forceKill(drachtio);
      forceKill(router);
      drachtio = null;
      router = null;
      done();
    }, 5000);

    if (router) {
      debug('killing router');
      try { router.kill(); } catch (e) { /* already dead */ }
      router = null;
    }

    if (drachtio) {
      debug('killing drachtio');
      drachtio.on('exit', () => {
        drachtio = null;
        debug('drachtio exited');
        clearTimeout(timer);
        done();
      });
      try { drachtio.kill('SIGTERM'); } catch (e) {
        drachtio = null;
        clearTimeout(timer);
        done();
      }
    } else {
      clearTimeout(timer);
      done();
    }
  });
};
