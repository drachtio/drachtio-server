const net = require('net');
const Emitter = require('events');
const crypto = require('crypto');
const debug = require('debug')('drachtio:server-test');

/**
 * A minimal drachtio wire-protocol client used to simulate a wedged
 * application: one that connects, authenticates and registers a route,
 * then stops reading from its socket -- as a hung or overloaded process
 * would.  Once its kernel receive buffer fills, the server's writes to it
 * stop completing, which is the condition that turned a dead client into
 * a routing zombie in production.
 *
 * Note: framing below treats string length as byte length, which holds
 * because the admin protocol traffic in these tests is all ASCII.
 */
class WedgeClient extends Emitter {
  constructor() {
    super();
    this.socket = null;
    this.pending = new Map();
    this.incoming = '';
    this.closed = false;
  }

  connect({host = '127.0.0.1', port = 9022, secret = 'cymru'} = {}) {
    return new Promise((resolve, reject) => {
      this.socket = net.connect({host, port}, () => {
        this._request(`authenticate|${secret}`)
          .then((tokens) => {
            if ('OK' !== tokens[0]) return reject(new Error('authentication failed'));
            resolve();
          })
          .catch(reject);
      });
      this.socket.setNoDelay(true);
      this.socket.on('data', (data) => this._onData(data));
      this.socket.on('error', (err) => debug(`wedge-client socket error: ${err.message}`));
      this.socket.on('close', () => {
        this.closed = true;
        this.emit('close');
      });
    });
  }

  route(verb) {
    return this._request(`route|${verb}`).then((tokens) => {
      if ('OK' !== tokens[0]) throw new Error(`route request for ${verb} refused`);
    });
  }

  /* stop reading from the socket, like a hung process: the kernel receive
     buffer fills, our tcp window closes, and the server's writes to us
     stop completing */
  wedge() {
    this.socket.pause();
  }

  /* fire off n pings without reading any responses; the server's replies
     pile up in its kernel send buffer until its writes to us jam */
  flood(n) {
    for (let i = 0; i < n; i++) this._write('ping');
  }

  disconnect() {
    if (this.socket) this.socket.destroy();
  }

  _write(msg) {
    const msgId = crypto.randomUUID();
    const s = `${msgId}|${msg}`;
    this.socket.write(`${Buffer.byteLength(s, 'utf8')}#${s}`);
    return msgId;
  }

  _request(msg) {
    return new Promise((resolve, reject) => {
      const msgId = this._write(msg);
      const timer = setTimeout(() => {
        this.pending.delete(msgId);
        reject(new Error(`timeout waiting for response to ${msg}`));
      }, 4000);
      this.pending.set(msgId, (tokens) => {
        clearTimeout(timer);
        resolve(tokens);
      });
    });
  }

  _onData(data) {
    this.incoming += data.toString();
    for (;;) {
      const idx = this.incoming.indexOf('#');
      if (-1 === idx) return;
      const len = parseInt(this.incoming.slice(0, idx), 10);
      const start = idx + 1;
      if (this.incoming.length < start + len) return;
      const payload = this.incoming.slice(start, start + len);
      this.incoming = this.incoming.slice(start + len);
      this._onMessage(payload);
    }
  }

  _onMessage(payload) {
    const tokens = payload.split('|');
    // response format: <uuid>|response|<request msgId>|OK|...
    if ('response' === tokens[1]) {
      const cb = this.pending.get(tokens[2]);
      if (cb) {
        this.pending.delete(tokens[2]);
        cb(tokens.slice(3));
      }
    }
    else if ('sip' === tokens[1]) {
      this.emit('sip', payload);
    }
  }
}

module.exports = WedgeClient;
