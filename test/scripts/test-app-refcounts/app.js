const Srf = require('drachtio-srf');
const srf = new Srf();
const regParser = require('drachtio-mw-registration-parser');
const waitFor = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const body = `v=0
o=- 0 0 IN IP4
s=-
c=IN IP4 127.0.0.1
t=0 0
m=audio 30000 RTP/AVP 0
a=rtpmap:0 PCMU/8000
`;
srf.connect({
  host: '127.0.0.1',
  port: 9022,
  secret: 'cymru'
});
srf.on('connect', (err, hp) => {
  console.log(`connected to drachtio listening on ${hp}`);
});

srf.use('register', regParser);

srf.register(async(req, res) => {
  const uri = req.getParsedHeader('Contact')[0].uri;
  console.log(`registering ${uri}`);
  res.send(200, {headers: {Expires: 3600}});
  await waitFor(32000);
  const uac = await srf.createUAC(uri, {
    headers: {
      'Call-ID': req.get('Call-ID')
    },
    localSdp: body
  });
  await waitFor(60000);
  uac.destroy();
});

