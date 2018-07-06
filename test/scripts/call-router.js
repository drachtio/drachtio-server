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
        uri: `127.0.0.1:${parseInt(arr[1])}`
      }
    });
  }
  

  res.json({action: 'reject', data: {status: 500}});
});
