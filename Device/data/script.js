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
    document.getElementById("camera").innerHTML = myObj["camera"];
    document.getElementById("ssid").innerHTML = myObj["ssid"];
    document.getElementById("softwareVersion").innerHTML = myObj["softwareVersion"];
    document.getElementById("rearCamActive").innerHTML = myObj["rearCamActive"];
    document.getElementById("trailCamActive").innerHTML = myObj["trailCamActive"];
    document.getElementById("ram").innerHTML = myObj["ram"];
    document.getElementById("uptime").innerHTML = myObj["uptime"];
    // values
    document.getElementById("currentCamera").value = myObj["currentCamera"];
    document.getElementById("sensorMin").value = myObj["sensorMin"];
    document.getElementById("sensorMax").value = myObj["sensorMax"];
    document.getElementById("sensorMin2").value = myObj["sensorMin2"];
    document.getElementById("sensorMax2").value = myObj["sensorMax2"];
    document.getElementById("frontCamTimeout").value = myObj["frontCamTimeout"];
    document.getElementById("trailerCamMode").value = myObj["trailerCamMode"];
    // checkboxes
    document.getElementById("serialPlotter").checked = myObj["serialPlotter"];
    document.getElementById("autoSwitch").checked = myObj["autoSwitch"];
    //if (myObj["camera"] == "rear")
    //    document.getElementById("0").checked = false;
    //else
    //    document.getElementById("0").checked = true;
    //console.log(event.data);
}

// Send Requests to Control GPIOs
function toggleCheckbox(element) {
    console.log('{"' + element.id + '":' + element.checked + '}');
    websocket.send('{"' + element.id + '":' + element.checked + '}');
    /*if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF"; 
    }*/
}
function enterNumber(element) {
    console.log('{"' + element.id + '":"' + element.value + '"}');
    websocket.send('{"' + element.id + '":"' + element.value + '"}');
    /*if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF"; 
    }*/
}