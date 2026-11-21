const assert = require('assert');
const net = require('net');
const tls = require('tls');
const Srf = require('drachtio-srf');
const config = require('./config');

// These peers deliberately share a Contact user/private host, but have distinct
// advertised ports and reliable connections, as separate accounts on one phone do.
class Peer {
  constructor(received, transport) {
    this.transport = transport;
    this.readyEvent = transport === 'tls' ? 'secureConnect' : 'connect';
    this.received = received;
    this.pending = new Map();
    this.buffer = '';
    this.sequence = 0;
    this.socket = transport === 'tls'
      ? tls.connect({port: 5090, host: '127.0.0.1', rejectUnauthorized: false})
      : net.connect(5090, '127.0.0.1');
    this.socket.setEncoding('utf8');
    this.socket.on('error', () => {});
    this.socket.on('data', data => {
      this.buffer += data;
      for (;;) {
        const end = this.buffer.indexOf('\r\n\r\n');
        if (end < 0) return;
        const head = this.buffer.slice(0, end);
        const length = Number(/Content-Length:\s*(\d+)/i.exec(head)?.[1] || 0);
        if (this.buffer.length < end + 4 + length) return;
        this.buffer = this.buffer.slice(end + 4 + length);
        const lines = head.split('\r\n');
        const headers = Object.fromEntries(lines.slice(1).map(line => {
          const colon = line.indexOf(':');
          return [line.slice(0, colon).toLowerCase(), line.slice(colon + 1).trim()];
        }));
        if (lines[0].startsWith('SIP/2.0')) {
          const status = Number(lines[0].split(' ')[1]);
          if (status >= 200) this.pending.get(headers['call-id'])?.({status, headers});
        } else {
          this.received.push({peer: this, method: lines[0].split(' ')[0], headers});
          this.socket.write([
            'SIP/2.0 200 OK',
            ...['via', 'from', 'to', 'call-id', 'cseq'].map(name => `${name}: ${headers[name]}`),
            'Content-Length: 0', '', ''
          ].join('\r\n'));
        }
      }
    });
  }

  request(method, contact, expires = 3600) {
    const callId = `registration-transport-${this.socket.localPort}-${++this.sequence}`;
    const promise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(callId);
        reject(new Error(`${method} response timed out`));
      }, 3000);
      this.pending.set(callId, response => {
        clearTimeout(timer);
        this.pending.delete(callId);
        resolve(response);
      });
    });
    this.socket.write([
      `${method} sip:service@127.0.0.1:5090 SIP/2.0`,
      `Via: SIP/2.0/${this.transport.toUpperCase()} 10.0.0.1:41001;branch=z9hG4bK-${callId};rport`,
      `From: <sip:101@account.example>;tag=${callId}`,
      'To: <sip:service@127.0.0.1>',
      `Call-ID: ${callId}`, `CSeq: ${this.sequence} ${method}`,
      `Contact: <${contact}>`, `Expires: ${expires}`,
      'Max-Forwards: 70', 'Content-Length: 0', '', ''
    ].join('\r\n'));
    return promise;
  }

  ack(response) {
    this.socket.write([
      'ACK sip:127.0.0.1:5090 SIP/2.0',
      `Via: SIP/2.0/${this.transport.toUpperCase()} 10.0.0.1:41001;branch=z9hG4bK-ack-${response.headers['call-id']};rport`,
      ...['from', 'to', 'call-id'].map(name => `${name}: ${response.headers[name]}`),
      `CSeq: ${response.headers.cseq.split(' ')[0]} ACK`,
      'Max-Forwards: 70', 'Content-Length: 0', '', ''
    ].join('\r\n'));
  }
}

module.exports = class RegistrationTransport {
  constructor() {
    this.srf = new Srf();
    this.peers = [];
    this.received = [];
    this.dialogs = new Map();
    this.srf.on('error', () => {});
    this.srf.register((req, res) => res.send(200, {
      headers: {Contact: req.get('Contact'), Expires: req.get('Expires')}
    }));
    this.srf.options((req, res) => res.send(200));
    this.srf.invite((req, res) => {
      const promise = this.srf.createUAS(req, res);
      promise.catch(() => {});
      this.dialogs.set(req.get('Call-ID'), promise);
    });
  }

  connect() {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('SRF connection timed out')), 3000);
      this.srf.once('connect', err => {
        clearTimeout(timer);
        if (err) reject(err);
        else resolve();
      });
      this.srf.connect(config.drachtio.connectOpts);
    });
  }

  async peer() {
    const peer = new Peer(this.received, this.transport);
    this.peers.push(peer);
    await new Promise((resolve, reject) => {
      peer.socket.once(peer.readyEvent, resolve);
      peer.socket.once('error', reject);
    });
    return peer;
  }

  async call(peer, contact) {
    const answer = await peer.request('INVITE', contact);
    assert.equal(answer.status, 200);
    peer.ack(answer);
    const dialog = await this.dialogs.get(answer.headers['call-id']);
    return dialog;
  }

  async run(args = {}) {
    this.transport = args.tls ? 'tls' : 'tcp';
    const scheme = args.tls ? 'sips' : 'sip';
    const contactA = `${scheme}:101@10.0.0.1${args.omitPort ? '' : ':41001'};transport=${this.transport}`;
    const contactB = `${scheme}:101@10.0.0.1:41002;transport=${this.transport}`;
    const a = await this.peer();
    const b = await this.peer();
    const registeredA = await a.request('REGISTER', contactA);
    assert.equal(registeredA.status, 200);
    const firstDialog = await this.call(a, contactA);
    const registeredB = await b.request('REGISTER', contactB);
    assert.equal(registeredB.status, 200);

    await firstDialog.destroy();
    assert.equal(this.received.at(-1).method, 'BYE');
    assert.equal(this.received.at(-1).peer, a, 'BYE must use account A despite account B registering last');

    const dialog = await this.call(a, contactA);
    const inDialog = await dialog.request({method: 'OPTIONS'});
    assert.equal(inDialog.status, 200);
    assert.equal(this.received.at(-1).peer, a, 'in-dialog request must use account A');
    const outbound = await new Promise((resolve, reject) => {
      this.srf.request({uri: contactA, method: 'OPTIONS'}, (err, request) => {
        if (err) return reject(err);
        request.on('response', response => {
          if (response.status >= 200) resolve(response);
        });
      });
    });
    assert.equal(outbound.status, 200);
    assert.equal(this.received.at(-1).peer, a, 'out-of-dialog lookup must include Contact port');

    // A refresh on a new connection must still replace this exact Contact's binding.
    const reconnected = await this.peer();
    const refreshedA = await reconnected.request('REGISTER', contactA);
    assert.equal(refreshedA.status, 200);
    const refreshed = await dialog.request({method: 'OPTIONS'});
    assert.equal(refreshed.status, 200);
    assert.equal(this.received.at(-1).peer, reconnected, 'same-Contact reconnect still works');

    // Unregistering B must not remove A's refreshed binding.
    const unregisteredB = await b.request('REGISTER', contactB, 0);
    assert.equal(unregisteredB.status, 200);
    await dialog.destroy();
    assert.equal(this.received.at(-1).method, 'BYE');
    assert.equal(this.received.at(-1).peer, reconnected, 'BYE must reach A after B unregisters');
  }

  disconnect() {
    for (const peer of this.peers) peer.socket.destroy();
    this.srf.disconnect();
  }
};
