
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

var outputPorts = {};

for (var i = 0; i < output.getPortCount(); i++) {
    outputPorts[output.getPortName(i)] = i;
    console.log(output.getPortName(i));
}

//output.openVirtualPort('SGI Dial Box A4 PP');
//output.openPort(outputPorts['Elektron Analog Four']);
output.openPort(outputPorts['Maschine Controller MIDI output port 0']);

var performance_cc = [
    3, 4, 8, 9, 64, 65, 66, 67
];

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
        if (values[i] < 0) {
            values[i] = 0;
        } else if (values[i] > 1270) {
            values[i] = 1270;
        }
        output.sendMessage([0xB0, performance_cc[i], Math.floor(values[i] / 10)]);
        console.log(values);
    }
});

