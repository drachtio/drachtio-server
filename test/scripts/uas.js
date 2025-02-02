const Emitter = require('events');
const Srf = require('drachtio-srf');
const parseUri = Srf.parseUri;
const config = require('./config');
const debug = require('debug')('drachtio:server-test');
const fs = require('fs');
const assert = require('assert');
const { RSA_NO_PADDING } = require('constants');

class App extends Emitter {
  constructor(tags) {
    super();

    this.srf = new Srf(tags) ;
    this.srf.on('error', (err) => {
      console.log(`Uas: error: ${err}`)
      //this.emit('error', err);
    });

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

    // for the test recv-notify-before-sending-200-ok
    this.srf.notify((req, res) => res.send(200));
  }
  
  connectTls(serverCert) {
    debug('connecting to drachtio server via tls');
    const ca = fs.readFileSync(serverCert);
    const opts = config.drachtioTls.connectOpts;
    opts.tls = { ca, rejectUnauthorized: false };

    this.srf.connect(opts);
    return new Promise((resolve, reject) => {
      this.srf.on('connect', (err) => {
        if (err) return reject(err);
        resolve();
      });
    });
  }

  listen(port) {
    debug(`listening for tcp connections on port ${port}`);
    return new Promise((resolve, reject) => {
      this.srf.listen({port, secret: 'cymru'}, () => resolve());
    });
  }

  listenTls(port, opts) {
    debug(`listening for tls connections on port ${port}`);
    return new Promise((resolve, reject) => {
      this.srf.listen({port, secret: 'cymru', tls: opts}, () => resolve());
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
      this.srf.proxyRequest(req, dest, {recordRoute: true})
        .catch((err) => {
          console.log(`Uas: proxy failed: ${err}`);
        });
    });
  }

  b2b(dest) {
    this.srf.invite((req, res) => {
      function end(srf, dlg) {
        dlg.destroy().catch((err) => {});
        srf.endSession(req);
      }
      this.srf.createB2BUA(req, res, dest)
        .then(({uas, uac}) => {
          this.emit('connected');
          uac.on('destroy', () => end(this.srf, uas));
          uas.on('destroy', () => end(this.srf, uac));
        })
        .catch((err) => {
          if (487 !== err.status) console.log(`Uas: failed to connect: ${err}`);
        });
    });
  }

  b2bdisconnect(dest) {
    this.srf.invite((req, res) => {
      function end(srf, dlg) {
        dlg.destroy().catch((err) => {});
        srf.endSession(req);
      }
      this.srf.createB2BUA(req, res, dest, {}, {
        cbRequest: () => {
          // deliberately drop the connection here
          this.srf.disconnect();
        }
      })
        .then(({uas, uac}) => {
          this.emit('connected');
          uac.on('destroy', () => end(this.srf, uas));
          uas.on('destroy', () => end(this.srf, uac));
        })
        .catch((err) => {
          if (487 !== err.status) console.log(`Uas: failed to connect: ${err}`);
        });
    });
  }

  accept(sdp, delay) {
    this.srf.invite((req, res) => {

      assert(req.server.address);
      assert(req.server.hostport);

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
  handleSessionExpired(sdp, delay) {
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

            uas.on('destroy', (bye, reason) => {
              assert(reason === 'Session timer expired');
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
  handleReinviteScenario(sdp, useBody) {
    return new Promise((resolve, reject) => {
      this.srf.invite((req, res) => {

        const localSdp = sdp || req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000');
  
        const opts = {};
        if (useBody) Object.assign(opts, {body: localSdp});
        else Object.assign(opts, {localSdp});
  
        this.srf.createUAS(req, res, opts)
          .then((uas) => {
            this.emit('connected', uas);
            
            uas.on('modify', (req, res) => {
              debug('re-invite received');
              res.send(200, {
                body: localSdp
                }, (err, response) => {
                  debug(`response sent: ${response}`);
                }, (ack) => {
                  debug('received ack');
                  uas.destroy();
                  resolve();
                }
              );
            });
          })
          .catch((err) => {
            console.error(`Uas: failed to connect: ${err}`);
            this.emit('error', err);
          });
      });
    });
  }

  handleReinviteByeRace(dest) {
    return new Promise((resolve) => {
      this.srf.invite(async(req, res) => {
        function end(srf, dlg) {
          dlg.destroy();
          //srf.endSession(req);
        }

        const {uas, uac} = await this.srf.createB2BUA(req, res, dest);
        [uas, uac].forEach((dlg) => dlg.on('destroy', () => end(this.srf, dlg.other), 300));

        uas.on('modify', (req, res) => {
          setTimeout(async() => {
            const sdp = await uac.modify(req.body);
            res.send(200, {body: sdp});
          }, 100);
        });
      });
    });
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

  doRegister() {
    this.srf.register((req, res) => {
      console.log('sending 200 OK to register');
      res.send(200);
    });
    return this;
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
