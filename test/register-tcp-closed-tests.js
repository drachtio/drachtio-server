const test = require('tape');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop, waitForPort, waitForPortInUse} = require('./testbed');

// Run standalone with: cd test && node register-tcp-closed-tests.js
// A client registers over TCP and disconnects; a later request to its Contact must
// not be forced onto the closed connection.
const sendOptions = (srf, uri) => new Promise((resolve, reject) => {
  srf.request(uri, {method: 'OPTIONS'}, (err, req) => {
    if (err) return reject(err);
    req.on('response', (res) => resolve(res));
  });
});

test('register over tcp: request to the contact after the connection closed', async(t) => {
  const contactPort = 5096;
  let srf;
  try {
    await start(null, []);
    srf = new Srf();
    srf.connect(config.drachtio.connectOpts);
    await new Promise((resolve, reject) => {
      srf.on('connect', (err) => err ? reject(err) : resolve());
      srf.on('error', reject);
    });
    srf.register((req, res) => res.send(200, {headers: {Expires: '60'}}));

    await waitForPort(contactPort).catch(() => {});
    const uas = execCmd(`sipp -sf ./uas-tcp-options-200.xml -i 127.0.0.1 -p ${contactPort} ` +
      '-t t1 -m 1 -timeout 15s -timeout_error', {cwd: './scenarios'});
    uas.catch(() => {});
    await waitForPortInUse(contactPort, 5000);

    await execCmd(`sipp -sf ./uac-tcp-register-then-close.xml -s alice -key contact_port ${contactPort} ` +
      '-i 127.0.0.1 -p 5095 -t t1 -m 1 -timeout 10s -timeout_error 127.0.0.1:5090', {cwd: './scenarios'});
    t.pass('registered over tcp, then the connection was closed');
    await delay(500);

    const res = await Promise.race([
      sendOptions(srf, `sip:alice@127.0.0.1:${contactPort};transport=tcp`),
      delay(5000).then(() => { throw new Error('no response to OPTIONS'); })
    ]);
    t.equal(res.status, 200, 'OPTIONS reached the contact over a new connection');
    await uas;
  } catch (err) {
    t.fail(`failed with error ${err}`);
  } finally {
    if (srf) srf.disconnect();
    await stop();
    t.end();
  }
});
