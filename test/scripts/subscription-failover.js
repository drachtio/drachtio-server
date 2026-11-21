const Srf = require('drachtio-srf');
const config = require('./config');

class SubscriptionFailoverApp {
  constructor() {
    this.clients = [new Srf(), new Srf()];
    this.initialClient = null;
    this.reject = null;

    this.clients.forEach((client) => {
      client.on('error', (err) => {
        if (this.reject) this.reject(err);
      });
    });
  }

  connect(connectArgs) {
    const options = connectArgs || config.drachtio.connectOpts;
    const connections = this.clients.map((client) => new Promise((resolve, reject) => {
      client.once('connect', (err) => {
        if (err) return reject(err);
        resolve();
      });
      client.connect({...options});
    }));

    return Promise.all(connections);
  }

  handleSubscriptionFailover() {
    return new Promise((resolve, reject) => {
      this.reject = reject;
      this.clients.forEach((client) => {
        client.subscribe((req, res) => {
          const isRefresh = /(?:^|;)\s*tag=/i.test(req.get('To'));

          if (!isRefresh) {
            if (this.initialClient) {
              res.send(500);
              return reject(new Error('initial SUBSCRIBE was delivered more than once'));
            }

            this.initialClient = client;
            return res.send(200, {headers: {Expires: 3600}}, (err) => {
              if (err) return reject(err);

              // Let the response reach SIPp before dropping the application
              // connection which owns the newly-created subscription dialog.
              setTimeout(() => client.disconnect(), 50);
            });
          }

          if (client === this.initialClient) {
            res.send(500);
            return reject(new Error('subscription refresh did not fail over'));
          }

          return res.send(200, {headers: {Expires: 3600}}, (err) => {
            if (err) return reject(err);
            resolve();
          });
        });
      });
    });
  }

  disconnect() {
    this.clients.forEach((client) => {
      try {
        client.disconnect();
      } catch (err) {
        // The client which owned the original dialog is already disconnected.
      }
    });
  }
}

module.exports = SubscriptionFailoverApp;
