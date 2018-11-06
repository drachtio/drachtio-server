const express = require('express');
const app = express();
const config = require('./config');
const debug = require('debug')('drachtio:server-test');
const argv = require('minimist')(process.argv.slice(2));
const transport = argv['transport'] === 'tls' ? 'tls' : 'tcp';

process.on('SIGTERM', () => {
  console.log('call router received SIGINT.');
  process.exit(0);
});
const server = app.listen(config.express.port, () => {
  debug(`call router app listening on ${JSON.stringify(server.address())} for http requests`);
});

app.get('/', (req, res) => {
  debug(`call-router: ${JSON.stringify(req.query)}`);
  debug(`started with ${JSON.stringify(argv)}`);

  if (['orange', 'red', 'blue', 'green', 'black'].includes(req.query.uriUser)) {
    return res.json({
      action: 'route',
      data: {
        tag: req.query.uriUser
      }
    });
  }

  let arr = /outbound-(\d+)/.exec(req.query.uriUser);
  if (arr) {
    return res.json({
      action: 'route',
      data: {
        uri: `127.0.0.1:${parseInt(arr[1])};transport=${transport}`
      }
    });
  }
  

  res.json({action: 'reject', data: {status: 500}});
});
