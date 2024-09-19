const test = require('tape');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop } = require('./testbed');
const logger = require('pino')({level: 'debug'});
let configFile, drachtio;
const manageServer = !process.env.NOSERVER;

/*
process.on('uncaughtException', (err, origin) => {
  console.log({err, origin}, 'uncaught exception');
});
*/
module.exports = async function runTests(testName) {
  const fixtures = require(`./test-fixtures-${testName}`);
  logger.debug(`starting test suite ${testName}, manageServer: ${manageServer}`);

  drachtio = undefined;
  configFile = undefined;
  for (const f of fixtures) {
    try {
      await runFixture(f);
    } catch (err) {
      logger.info({err}, `Error running test ${f.message}`);
    }
  }

  await stop().catch((err) => logger.info({err}, 'Error shutting down dracthio after tests'));
  logger.info(`completed test suite ${testName}`);
};

function runFixture(f) {
  return new Promise((resolve, reject) => {
    logger.debug({f, configFile}, 'running test');
    test(f.message, async(t) => {
      logger.debug('setting timeout');

      t.timeoutAfter(f.timeout || 10000);
      try {
        /**
         * If a new drachtio config is required, start a new server
         */
        if (manageServer) {
          logger.debug('checking server');
          if (configFile !== f.server.config) {
            logger.debug('starting new server');
            configFile = f.server.config;
            const obj = f.server;

            if (drachtio) {
              logger.debug('stopping drachtio server');
              await stop();
            }
            logger.debug({obj}, 'starting new drachtio server');
            drachtio = await start(obj.config, obj.args, obj.tls || false, obj.waitDelay || 1000, obj.env || {});
          }
        }

        /**
         * If we have a UAS sipp scenario, start that first
         * Next, if we have a node.js script, start that
         * Finally, if we have a UAC sipp scenario, start that last
         *
         * If we have all 3, test passes if the UAC scenario runs without error
         * If we don't have a UAC, test passes if the node.js script runs without error
         */
        let uasPromise, scriptPromise, uacPromise;
        let script;
        if (f.uas) {
          let cmd = `sipp -sf ./${f.uas.name} -i 127.0.0.1 -p ${f.uas.port} -m 1`;
          if (f.uas.transport === 'tcp') cmd += ' -t t1';
          logger.debug(`starting UAS scenario: ${cmd}`);
          uasPromise = execCmd(cmd, {cwd: './scenarios'});
          await delay(1000);
        }
        if (f.script) {
          logger.debug({script: f.script}, 'starting node.js script');
          const Script = require(`./scripts/${f.script.name}`);
          script = new Script();
          await script.connect(f.script.connectArgs);
          logger.debug('connected ok');
          if (f.script.function) {
            const args = f.script.args || (f.uas ? `127.0.0.1:${f.uas.port}` : undefined);
            scriptPromise = script[f.script.function](args, f.script.opts || {});
            if (f.script.delay) await delay(f.script.delay);
          }
        }
        if (f.uac) {
          const cid_str = '-cid_str %u-%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p' +
            '%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p' +
            '%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p%p@%s';
          let cmd = `sipp -sf ./${f.uac.name} ${f.uac.target} ${f.uac.long_call_id ? cid_str : ''} -m 1`;
          if (f.uac.transport === 'tcp') cmd += ' -t t1';
          logger.debug(`starting UAC scenario: ${cmd}`);
          uacPromise = execCmd(cmd, {cwd: './scenarios'});
        }

        try {
          const promises = [];
          [uasPromise, scriptPromise, uacPromise].forEach((p) => {
            p && promises.push(p);
          });
          if (promises.length > 0) {
            logger.debug(`waiting for ${promises.length} promises to resolve..`);
            await Promise.all(promises);
          }
        } catch (err) {
          logger.debug({err}, 'caught error in script');
          if (![err.message, err, '*'].includes(f.script.error)) {
            logger.error('unexpected error in script - rethrowing');
            throw err;
          }
        };

        try {
          if (script) script.disconnect();
          //logger.debug('waiting 10 secs..');
          //await delay(10000);
          //logger.debug('waiting sipp UAS to finish');
          if (uasPromise) await uasPromise;
          //logger.debug('waiting sipp UAC to finish');
          if (uacPromise) await uacPromise;
        } catch (err) {
          logger.info(err, 'ignoring error');  
        }
        logger.debug({f}, 'completed test');
        t.pass(`${f.message}`);
        t.end();

        resolve();
      } catch (err) {
        logger.error(err, 'Error in test');
        t.end(err);
        reject(err);
      }
    });
  });
}
