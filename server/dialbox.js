
midi = require('midi');

var values = [0,0,0,0,0,0,0,0];

var input = new midi.input();
var portNumber = undefined;

for (var i = 0; i < input.getPortCount(); i++) {
    if (input.getPortName(i) == 'SGI Dial Box') {
        portNumber = i;
        break;
    }
}

if (portNumber == undefined) {
    throw new Error('could not find SGI Dial Box');
}

input.openPort(portNumber);

var output = new midi.output();

output.openVirtualPort('SGI Dial Box CC');

input.on('message', function (deltaTime, message) {
    if ((message[0] == 0xB0)
        && (message[1] >= 102)
        && (message[1] <= 118)) {
        var cc = message[1] - 102;
        var i = cc >> 1;
        var value = message[2];
        if (cc & 1) {
            values[i] += value * value;
        } else {
            values[i] -= value * value;
        }
        output.sendMessage([0xB0, i + 80, Math.abs(values[i] % 127)]);
        console.log(values);
    }
});

