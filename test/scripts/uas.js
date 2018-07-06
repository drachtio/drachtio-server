const Emitter = require('events');
const Srf = require('drachtio-srf');
const parseUri = Srf.parseUri;
const config = require('./config');
const debug = require('debug')('drachtio:server-test');

class App extends Emitter {
  constructor(tags) {
    super();

    this.srf = new Srf(tags) ;
    this.srf.on('error', (err) => { this.emit('error', err);});

    this.calls = 0;
  }

  connect() {
    this.srf.connect(config.drachtio.connectOpts);
    return new Promise((resolve, reject) => {
      this.srf.on('connect', (err) => {
        if (err) return reject(err);
        resolve();
      });
    });
  }

  listen(port) {
    debug(`listening on port ${port}`);
    return new Promise((resolve, reject) => {
      this.srf.listen({port, secret: 'cymru'}, () => resolve());
    });
  }

  reject(code, headers) {
    this.srf.invite((req, res) => {
      res.send(code, {headers});
    });
    return this;
  }

  proxy(dest) {
    this.srf.invite((req, res) => {
      this.srf.proxyRequest(req, dest, {recordRoute: true});
    });
  }

  accept(sdp, delay) {
    this.srf.invite((req, res) => {

      this.calls++;
      req.on('cancel', () => {
        req.canceled = true;
      });
      const localSdp = sdp || req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000');

      setTimeout(() => {
        if (req.canceled) return;

        const headers = {} ;
        const uri = parseUri(req.uri);
        if (['orange', 'red', 'blue', 'green'].includes(uri.user)) {
          Object.assign(headers, {'X-Color': uri.user});
        }
        this.srf.createUAS(req, res, {localSdp, headers})
          .then((uas) => {
            this.emit('connected');

            return uas
              .on('refresh', () => { debug('dialog refreshed');})
              .on('modify', () => { debug('dialog modify');})
              .on('hold', () => { debug('dialog hold');})
              .on('unhold', () => { debug('dialog unhold');})
              .on('destroy', () => {
                this.srf.endSession(req);
                debug('received BYE from uac');
              });
          })
          .catch((err) => {
            console.error(`Uas: failed to connect: ${err}`);
          });
      }, delay || 1);
    });

    return this;
  }

  handleReinvite(sdp, delay) {
    this.srf.invite((req, res) => {

      req.on('cancel', () => {
        req.canceled = true;
      });
      const localSdp = sdp || req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000');

      setTimeout(() => {
        if (req.canceled) return;

        this.srf.createUAS(req, res, {localSdp})
          .then((uas) => {
            this.emit('connected');

            uas.on('destroy', () => {
              debug('received BYE from uac');
            });
            return;
          })
          .catch((err) => {
            console.error(`Uas: failed to connect: ${err}`);
          });
      }, delay || 1);
    });

    return this;
  }

  disconnect() {
    this.srf.disconnect();
    return this;
  }

  // bogus things an app could do
  answerTwice(sdp, delay) {
    this.srf.invite((req, res) => {

      req.on('cancel', () => {
        req.canceled = true;
      });
      const localSdp = sdp || req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000');

      res.send(200, {body: localSdp});
      res.send(200, {body: localSdp});

      this.srf.bye((req, res) => {
        res.send(200);
      });
      return this;
    });
  }

  doPresence1() {
    // respond to publish and register immediately
    this.srf.register((req, res) => {
      console.log('sending 200 OK to register');
      res.send(200);
    });
    this.srf.publish((req, res) => {
      console.log('sending 200 OK to publish');
      res.send(200);
    });

    // slight delay in responding to subscribe
    this.srf.subscribe((req, res) => {
      console.log('got SUBSCRIBE');
      setTimeout(() => {
        console.log('sending 200 OK to SUBSCRIBE');
        this.srf.createUAS(req, res)
          .then((dlg) => {
            console.log('sending NOTIFY');
            return dlg.request({
              method: 'NOTIFY',
              headers: {
                'Subscription-State': 'active',
                'Event': req.get('Event')
              }
            });
          })
          .catch((err) => console.log(`Error responding to SUBSCRIBE: ${err}`));
      }, 200);
    });

    return this;
  }

  doPresence2() {
    // respond to publish and register immediate
    this.srf.register((req, res) => {
      console.log('sending 200 OK to register');
      res.send(200);
    });
    this.srf.publish((req, res) => {
      console.log('sending 200 OK to publish');
      res.send(200);
    });

    // slight delay in responding to subscribe
    this.srf.subscribe((req, res) => {
      console.log('got SUBSCRIBE');
      setTimeout(() => {
        console.log('sending 200 OK to SUBSCRIBE');
        res.send(200, (err, msg) => {
          console.log(`To header on 200 OK to SUBSCRIBE: ${msg.get('To')}`);
        });
        this.srf.request('sip:placeholder', {
          method: 'NOTIFY',
          headers: {
            'Call-ID': req.get('Call-ID'),
            'Subscription-State': 'active',
            'Event': req.get('Event')
          }
        }, (err, msg) => {
          console.log(`From header on NOTIFY: ${msg.get('From')}`);
        });
      }, 1);
    });

    return this;
  }

}

module.exports = App;
