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
    document.getElementById("currentcamera").innerHTML = myObj["camera"];
    document.getElementById("ssid").innerHTML = myObj["ssid"];
    document.getElementById("version").innerHTML = myObj["version"];
    document.getElementById("rearsensor").innerHTML = myObj["rearsensor"];
    document.getElementById("racksensor").innerHTML = myObj["racksensor"];
    document.getElementById("ram").innerHTML = myObj["ram"];
    document.getElementById("uptime").innerHTML = myObj["uptime"];
    //if (myObj["camera"] == "rear")
    //    document.getElementById("0").checked = false;
    //else
    //    document.getElementById("0").checked = true;
    console.log(event.data);
}

// Send Requests to Control GPIOs
function toggleCheckbox(element) {
    console.log(element.id);
    websocket.send(element.id);
    /*if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF"; 
    }*/
}
function enterNumber(element) {
    console.log(element.value);
    websocket.send(element.value);
    /*if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF"; 
    }*/
}