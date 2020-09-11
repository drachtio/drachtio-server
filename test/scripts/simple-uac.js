const Srf = require('drachtio-srf');
const srf = new Srf();

srf.connect({
  host: '127.0.0.1',
  port: 9022,
  secret: 'cymru'
})
  .on('connect', async(err, hp) => {
    console.log(`connected to ${hp}`);
    const dlg = await srf.createUAC('sip:jambonz.us', {
      proxy: 'sip:8.8.8.8',
      headers: {
        'Contact': 'sip:192.168.1.179'
      }
    });
    //const dlg = await srf.createUAC(`127.0.0.1:5062`, {});
    dlg.destroy();
  });

