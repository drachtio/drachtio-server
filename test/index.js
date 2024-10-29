const runTest = require('./run_tests');

async function main() {
  await runTest('uac-reinvite-stale-ack.json');
}

main();

