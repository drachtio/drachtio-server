const Srf = require('drachtio-srf');
const srf = new Srf();

srf.connect({
  host: '127.0.0.1',
  port: 9022,
  secret: 'cymru'
})
  .on('connect', async(err, hp) => {
    console.log(`connected to ${hp}`);
    //srf.request('sip:hosted.sip2sip.net', {
    srf.request('sip:194.50.56.35', {
      method: 'REGISTER',
      headers: {
        'From': '<sip:123458010@hosted.sip2sip.net>;tag=bkbfsda5jb',
        'To': '<sip:123458010@hosted.sip2sip.net>',
        'Contact': '<sip:123458010@localhost>;expires=1200'
      }
    });
  });

