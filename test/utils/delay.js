module.exports = (d) => {
  return new Promise((resolve) => {
    setTimeout(() => resolve(), d || 1000);
  });
};

