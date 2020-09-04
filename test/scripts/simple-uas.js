const Srf = require('drachtio-srf');
const srf = new Srf();

srf.connect({
  host: '127.0.0.1',
  port: 9022,
  secret: 'cymru'
})
  .on('connect', (err, hp) => console.log(`connected to ${hp}`));

srf.invite(async(req, res) => {
  const dlg = await srf.createUAS(req, res, {localSdp: req.body});
  console.log('call connected');
  dlg.on('destroy', () => {

  });
})