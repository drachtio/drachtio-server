const test = require('tape');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop, waitForPort, waitForPortInUse} = require('./testbed');

// Run standalone with: cd test && node uac-2xx-retransmit-tests.js
// The far end retransmits its 200 OK over TCP before the app sends the ACK, then hangs up.
const sdp = `v=0
o=user1 53655765 2353687637 IN IP4 127.0.0.1
s=-
c=IN IP4 127.0.0.1
t=0 0
m=audio 5000 RTP/AVP 0
a=rtpmap:0 PCMU/8000
`;

test('uac: 200 OK retransmitted over TCP before the ACK keeps the dialog', async(t) => {
  const port = 5097;
  let srf;
  try {
    await start(null, []);
    srf = new Srf();
    srf.connect(config.drachtio.connectOpts);
    await new Promise((resolve, reject) => {
      srf.on('connect', (err) => err ? reject(err) : resolve());
      srf.on('error', reject);
    });

    await waitForPort(port).catch(() => {});
    const uas = execCmd(`sipp -sf ./uas-tcp-200-retransmit-then-bye.xml -i 127.0.0.1 -p ${port} ` +
      '-t t1 -m 1 -timeout 15s -timeout_error', {cwd: './scenarios'});
    uas.catch(() => {});
    await waitForPortInUse(port, 5000);

    // late-offer INVITE: the app sends the ACK, with its answer, when it is ready
    const {ack} = await srf.createUAC(`sip:127.0.0.1:${port};transport=tcp`, {noAck: true});
    // leave time for the retransmission to arrive before the ACK goes out
    await delay(300);
    const dlg = await ack(sdp);
    await Promise.race([
      new Promise((resolve) => dlg.on('destroy', resolve)),
      delay(5000).then(() => { throw new Error('BYE never reached the application'); })
    ]);
    t.pass('BYE from the far end reached the application');
    await uas;
    t.pass('far end received 200 OK to its BYE');
  } catch (err) {
    t.fail(`failed with error ${err}`);
  } finally {
    if (srf) srf.disconnect();
    await stop();
    t.end();
  }
});
