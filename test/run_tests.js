const test = require('tape');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop, waitForPort, waitForPortInUse } = require('./testbed');
const logger = require('pino')({level: 'debug'});
const manageServer = !process.env.NOSERVER;

const LOG_DIR = process.env.DRACHTIO_TEST_LOG_DIR || '/tmp';

// Sanitize a fixture message for use as a file name component.
function logSlug(idx, msg) {
  const safe = String(msg || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 60);
  return `drachtio-fixture-${String(idx).padStart(2, '0')}-${safe}.log`;
}

// Wrap a Promise with a timeout. On timeout, the underlying work is NOT
// cancelled (we have no handle to it), but the returned Promise rejects so
// the caller can move on. Avoids tape's t.timeoutAfter — that path calls
// t.end() internally on timeout, which then cascades into ".end() already
// called" when our own t.end() runs and corrupts the framework's state for
// every subsequent fixture.
function withTimeout(promise, ms, label) {
  let handle;
  const timeoutPromise = new Promise((_, rej) => {
    handle = setTimeout(() => rej(new Error(`${label} timed out after ${ms}ms`)), ms);
  });
  return Promise.race([promise, timeoutPromise]).finally(() => clearTimeout(handle));
}

module.exports = async function runTests(testName) {
  const fixtures = require(`./test-fixtures-${testName}`);
  logger.debug(`starting test suite ${testName}, manageServer: ${manageServer}`);

  for (let i = 0; i < fixtures.length; i++) {
    try {
      await runFixture(fixtures[i], i);
    } catch (err) {
      // runFixture is now designed to never throw — it always resolves so the
      // loop continues even when a fixture fails or times out. Log defensively
      // in case something slipped through.
      logger.info({err}, `Error running test ${fixtures[i].message}`);
    }
  }

  await stop().catch((err) => logger.info({err}, 'Error shutting down dracthio after tests'));
  logger.info(`completed test suite ${testName}`);
};

function runFixture(f, fixtureIndex) {
  return new Promise((resolveOuter) => {
    logger.debug({f, fixtureIndex}, 'running test');
    test(f.message, async(t) => {

      // Single-shot end: tape's "end already called" cascade is what hangs CI.
      // Guarantee t.end() runs exactly once regardless of timeout / error path.
      let ended = false;
      const safeEnd = (err) => {
        if (ended) return;
        ended = true;
        if (err) {
          const msg = (err && (err.message || err.toString())) || String(err);
          t.fail(msg);
        }
        try { t.end(); } catch (e) { /* tape state was poisoned somehow; swallow */ }
        resolveOuter();
      };

      const timeoutMs = f.timeout || 10000;
      let uasPromise, scriptPromise, uacPromise;
      let script;

      try {
        // Per-fixture isolation: always restart drachtio + call-router. The
        // previous "reuse drachtio when config matches" optimisation saved a
        // few seconds across the suite but let one failed fixture poison
        // every subsequent same-config fixture (leaked dialogs, half-closed
        // sockets, in-progress transactions). Cost of fresh-per-fixture is
        // ~0.5–1s × N fixtures and is well within the 20-min CI budget.
        if (manageServer) {
          await stop().catch(() => {});
          const obj = f.server;
          const logPath = `${LOG_DIR}/${logSlug(fixtureIndex, f.message)}`;
          logger.debug({obj, logPath}, 'starting fresh drachtio for fixture');
          await start(obj.config, obj.args, obj.tls || false, obj.waitDelay || 1000, obj.env || {}, logPath);
        }

        /**
         * If we have a UAS sipp scenario, start that first
         * Next, if we have a node.js script, start that
         * Finally, if we have a UAC sipp scenario, start that last
         *
         * If we have all 3, test passes if the UAC scenario runs without error
         * If we don't have a UAC, test passes if the node.js script runs without error
         */
        if (f.uas) {
          await waitForPort(f.uas.port).catch((err) => {
            logger.warn(`port ${f.uas.port} still in use, proceeding anyway: ${err.message}`);
          });
          let cmd = `sipp -sf ./${f.uas.name} -i 127.0.0.1 -p ${f.uas.port} -m 1`;
          if (f.uas.transport === 'tcp') cmd += ' -t t1';
          logger.debug(`starting UAS scenario: ${cmd}`);
          uasPromise = execCmd(cmd, {cwd: './scenarios'});
          // Replace the old fixed 1s delay with explicit port-bound check.
          // sipp can take >1s to bind on slow CI runners with -t t1 (TCP).
          await waitForPortInUse(f.uas.port, 5000).catch((err) => {
            logger.warn(`UAS port ${f.uas.port} not bound after 5s: ${err.message}`);
          });
        }
        if (f.script) {
          logger.debug({script: f.script}, 'starting node.js script');
          const Script = require(`./scripts/${f.script.name}`);
          script = new Script();
          await script.connect(f.script.connectArgs);
          logger.debug('connected ok');
          if (f.script.function) {
            // When the fixture didn't supply explicit args, synthesise the
            // dialer destination from the UAS port. Critical: include the
            // UAS transport when set, otherwise drachtio-srf's createB2BUA
            // sends the B-leg via UDP regardless of how the UAS is listening
            // — which is the root cause of the long-sdp 503 cascade in CI.
            const args = f.script.args || (f.uas
              ? (f.uas.transport
                ? `sip:127.0.0.1:${f.uas.port};transport=${f.uas.transport}`
                : `127.0.0.1:${f.uas.port}`)
              : undefined);
            scriptPromise = script[f.script.function](args, f.script.opts || {});
            if (f.script.delay) await delay(f.script.delay);
          }
        }
        // Brief pause so route registration is processed by drachtio before
        // the UAC sends. Route registration is fast — 250 ms is enough; the
        // failure mode this replaces (1000 ms) was timing-on-port-bind, which
        // waitForPortInUse above now handles directly.
        if (f.script && f.uac) await delay(250);

        if (f.uac) {
          const cid_str = '-cid_str %u-%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p' +
            '%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p' +
            '%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p@%s';
          let cmd = `sipp -sf ./${f.uac.name} ${f.uac.target} ${f.uac.long_call_id ? cid_str : ''} -m 1`;
          if (f.uac.transport === 'tcp') cmd += ' -t tn';
          logger.debug(`starting UAC scenario: ${cmd}`);
          uacPromise = execCmd(cmd, {cwd: './scenarios'});
        }

        // Some script functions (e.g. uas.reject) return `this` rather than a
        // Promise, so filter to genuine thenables before attaching the
        // unhandled-rejection guard. Promise.all itself coerces non-thenables.
        const promises = [uasPromise, scriptPromise, uacPromise].filter(Boolean);
        const thenables = promises.filter((p) => typeof p.then === 'function');
        // Attach no-op catches up front so promises that reject after our
        // timeout fires don't surface as unhandled rejections.
        thenables.forEach((p) => p.catch(() => {}));

        try {
          if (promises.length > 0) {
            logger.debug(`waiting for ${promises.length} promises to resolve (timeout ${timeoutMs}ms)..`);
            await withTimeout(Promise.all(promises), timeoutMs, `fixture "${f.message}"`);
          }
        } catch (err) {
          logger.debug({err: err.message}, 'caught error in fixture body');
          if (!f.script || ![err.message, err, '*'].includes(f.script.error)) {
            // Real failure (or timeout) — fall through to safeEnd(err) below.
            throw err;
          }
        }

        try {
          if (script) script.disconnect();
          if (uasPromise) await withTimeout(uasPromise, 2000, 'uas drain').catch(() => {});
          if (uacPromise) await withTimeout(uacPromise, 2000, 'uac drain').catch(() => {});
        } catch (err) {
          logger.info(err, 'ignoring error during drain');
        }
        logger.debug({f: f.message}, 'completed test');
        t.pass(`${f.message}`);
        safeEnd();
      } catch (err) {
        logger.error({err: err.message, fixture: f.message}, 'fixture failed');
        try { if (script) script.disconnect(); } catch (e) { /* ignore */ }
        // Do not await uasPromise/uacPromise here on failure: if the fixture
        // timed out, those Promises may never resolve. The next fixture's
        // stop()+start() will reset all state.
        safeEnd(err);
      }
    });
  });
}
