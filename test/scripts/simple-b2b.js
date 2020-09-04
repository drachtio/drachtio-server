const Srf = require('drachtio-srf');
const srf = new Srf();

srf.connect({
  host: '127.0.0.1',
  port: 9022,
  secret: 'cymru'
})
  .on('connect', (err, hp) => console.log(`connected to ${hp}`));

srf.invite(async(req, res) => {
  const {uas, uac} = await srf.createB2BUA(req, res, '127.0.0.1:5062');
  console.log('call connected');
  [uas, uac].forEach((dlg) => {
    dlg.on('destroy', () => {
      dlg.other.destroy();
      console.log('call ended');
    });
  })
});
