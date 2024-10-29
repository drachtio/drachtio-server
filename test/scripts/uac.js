const Emitter = require('events');
const Srf = require('drachtio-srf');
const config = require('./config');
const assert = require('assert');
const debug = require('debug')('drachtio:server-test');

const defaultSdp = `v=0
o=user1 53655765 2353687637 IN IP4 127.0.0.1
s=-
c=IN IP4 127.0.0.1
t=0 0
m=audio 5000 RTP/AVP 0 101
a=rtpmap:0 PCMU/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-11,16`;

class App extends Emitter {
  constructor() {
    super();

    this.srf = new Srf() ;
    this.srf.on('error', (err) => { 
      console.log(`Uas: error: ${err}`)
      //this.emit('error', err);
    });
  }

  options(uri) {
    return this.srf.request({
      uri,
      method: 'OPTIONS'
    });
  }

  invite(uri) {
    return this.srf.request({
      uri,
      method: 'INVITE'
    });
  }

  connect(opts) {
    return new Promise((resolve, reject) => {
      this.srf.on('connect', (err, hp) => {
        debug(`Uac: connected to ${hp}`);
        if (err) return reject(err);
        resolve();
      });
      this.srf.connect(opts || config.drachtio.connectOpts);
    });
  }

  call(uri, opts) {
    assert(typeof uri === 'string');
    opts = opts || {};

    let sdp;
    if (typeof opts === 'string') sdp = opts;
    else if (opts.sdp) sdp = opts.sdp;
    else sdp = defaultSdp;

    setTimeout(() => {
      this.srf.createUAC(uri, {
        localSdp: sdp,
        headers: opts.headers
      }, {
        cbRequest: (err, req) => {
          if (opts.cancelAfter) {
            setTimeout(() => {
              debug('canceling uac call');
              req.cancel();
            }, opts.cancelAfter);
          }
        }
      })
        .then((uac) => {
          debug(`uac connected: ${JSON.stringify(uac)}`);
          this.emit('success', uac);
          this.uac = uac;

          if (opts.hangupAfter) {
            setTimeout(() => {
              uac.destroy();
            }, opts.hangupAfter);
          }
          if (opts.reinviteAfter) {
            setTimeout(() => {
              uac.modify(sdp);
            }, opts.reinviteAfter);
          }

          return uac
            .on('refresh', () => { debug('dialog refreshed');})
            .on('modify', () => { debug('dialog modify');})
            .on('hold', () => { debug('dialog hold');})
            .on('unhold', () => { debug('dialog unhold');})
            .on('destroy', () => { debug('received BYE from uas');});
        })
        .catch((err) => {
          this.emit('fail', err);
        });
    }, opts.delay || 500);
    return this;
  }

  call5secs(uri, opts) {
    assert(typeof uri === 'string');
    opts = opts || {};

    let sdp;
    if (typeof opts === 'string') sdp = opts;
    else if (opts.sdp) sdp = opts.sdp;
    else sdp = defaultSdp;

    setTimeout(() => {
      this.srf.createUAC(uri, {
        localSdp: sdp,
        headers: opts.headers
      }, {
        cbRequest: (err, req) => {
          if (opts.cancelAfter) {
            setTimeout(() => {
              debug('canceling uac call');
              req.cancel();
            }, opts.cancelAfter);
          }
        }
      })
        .then((uac) => {
          debug(`uac connected: ${JSON.stringify(uac)}`);
          this.emit('success', uac);
          this.uac = uac;

          setTimeout(() => {
            uac.destroy();
          }, 5000);
          if (opts.reinviteAfter) {
            setTimeout(() => {
              uac.modify(sdp);
            }, opts.reinviteAfter);
          }

          return uac
            .on('refresh', () => { debug('dialog refreshed');})
            .on('modify', () => { debug('dialog modify');})
            .on('hold', () => { debug('dialog hold');})
            .on('unhold', () => { debug('dialog unhold');})
            .on('destroy', () => { debug('received BYE from uas');});
        })
        .catch((err) => {
          this.emit('fail', err);
        });
    }, opts.delay || 500);
    return this;
  }

  reinvite_short_call(uri, opts) {
    assert(typeof uri === 'string');
    opts = opts || {};

    let sdp;
    if (typeof opts === 'string') sdp = opts;
    else if (opts.sdp) sdp = opts.sdp;
    else sdp = defaultSdp;

    setTimeout(() => {
      this.srf.createUAC(uri, {
        localSdp: sdp,
        headers: opts.headers
      }, {
        cbRequest: (err, req) => {
          if (opts.cancelAfter) {
            setTimeout(() => {
              debug('canceling uac call');
              req.cancel();
            }, opts.cancelAfter);
          }
        }
      })
        .then((uac) => {
          debug(`uac connected: ${JSON.stringify(uac)}`);
          this.emit('success', uac);
          this.uac = uac;

          // immediately reinvite
          uac.modify(sdp);

          // hangup after 5 seconds
          setTimeout(() => {
            uac.destroy();
          }, 10000);

          return uac
            .on('refresh', () => { debug('dialog refreshed');})
            .on('modify', () => { debug('dialog modify');})
            .on('hold', () => { debug('dialog hold');})
            .on('unhold', () => { debug('dialog unhold');})
            .on('destroy', () => { debug('received BYE from uas');});
        })
        .catch((err) => {
          this.emit('fail', err);
        });
    }, opts.delay || 500);
    return this;
  }


  hangup() {
    assert(this.uac);
    const p = this.srf.destroy(this.uac);
    this.uac = null;
    return p;
  }

  disconnect() {
    try {
      this.srf.disconnect();
    } catch (err) {
      console.log('got error');
      console.error(err);
    }
    return this;
  }
}

module.exports = App;
