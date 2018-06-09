const express = require('express');
const app = express();
const config = require('./config');
const debug = require('debug')('drachtio:server-test');


const server = app.listen(config.express.port, () => {
  debug(`call router app listening on ${JSON.stringify(server.address())} for http requests`);
});

app.get('/', (req, res) => {
  debug(`call-router: ${JSON.stringify(req.query)}`);

  if (['orange', 'red', 'blue', 'green', 'black'].includes(req.query.uriUser)) {
    return   res.json({
      action: 'route',
      data: {
        tag: req.query.uriUser
      }
    });
  }

  res.json({action: 'reject', data: {status: 500}});
});
