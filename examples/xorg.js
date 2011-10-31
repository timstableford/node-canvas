var Canvas = require('..');
var Util = require('util');

var xorg = new Canvas.backends.XorgBackend(320, 240);
var canvas = new Canvas.Canvas(xorg);

console.log('Width: ' + canvas.width + ', Height: ' + canvas.height);

var ctx = canvas.getContext('2d');
ctx.fillStyle = '#00FF00';
ctx.fillRect(50, 50, 100, 100);

(async() => {
    setTimeout(() => {
        xorg.abort();
    }, 5000);

    // You need to poll for messages or otherwise the initial draw doesn't happen.
    // This function also listens for mouse and key-presses in the window.
    while (true) {
        const result = await Util.promisify(xorg.poll.bind(xorg))();
        console.log(result);
        if (result.type == 'message' && result.value == 'exit') {
            break;
        }
    }
    console.log('polling aborted');
})();
