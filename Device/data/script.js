var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    var myObj = JSON.parse(event.data);
    console.log(myObj);
    // stats
    stats = myObj["stats"];
    for (var k in stats) {
        console.log(k, stats[k]);
        document.getElementById(k).innerHTML = stats[k];
    }
    // values
    settings = myObj["settings"];
    for (var k in settings) {
        console.log(k, settings[k]);
        document.getElementById(k).value = settings[k];
    }
    // checkboxes
    checkboxes = myObj["checkboxes"];
    for (var k in checkboxes) {
        console.log(k, checkboxes[k]);
        document.getElementById(k).checked = checkboxes[k];
    }
}

// Checkboxes
function toggleCheckbox(element) {
    console.log('{"' + element.id + '":' + element.checked + '}');
    websocket.send('{"' + element.id + '":' + element.checked + '}');
}
// Settings/values
function enterValue(element) {
    console.log('{"' + element.id + '":"' + element.value + '"}');
    websocket.send('{"' + element.id + '":"' + element.value + '"}');
}
// Running commands
function runCommand(element) {
    console.log('{"command":"' + element.id + '"}');
    websocket.send('{"command":"' + element.id + '"}');
}