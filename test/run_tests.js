const test = require('tape');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop } = require('./testbed');
const logger = require('pino')({level: 'info'});
let configFile, drachtio;
const manageServer = !process.env.NOSERVER;

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
        }
        if (f.script) {
          logger.debug({script: f.script}, 'starting node.js script');
          const Script = require(`./scripts/${f.script.name}`);
          script = new Script();
          await script.connect(f.script.connectArgs);
          if (f.script.function) {
            const args = f.script.args || (f.uas ? `127.0.0.1:${f.uas.port}` : undefined);
            scriptPromise = script[f.script.function](args);
          }
        }
        if (f.uac) {
          let cmd = `sipp -sf ./${f.uac.name} ${f.uac.target} -m 1`;
          if (f.uac.transport === 'tcp') cmd += ' -t t1';
          logger.debug(`starting UAC scenario: ${cmd}`);
          uacPromise = execCmd(cmd, {cwd: './scenarios'});
        }

        if (uacPromise) await uacPromise;
        else if (scriptPromise) {
          try {
            await scriptPromise;
          }
          catch (err) {
            if (![err, '*'].includes(f.script.error)) throw err;
          }
        }

        try {
          await delay(100);
          logger.debug('waiting for script to finish');
          if (script) await script.disconnect();
          logger.debug('waiting sipp UAS to finish');
          if (uasPromise) await uasPromise;
          logger.debug('waiting sipp UAC to finish');
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
