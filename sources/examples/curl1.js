if(typeof(ivs) == 'undefined') {
    throw "Illegal runtime";
}

// ----------------------------------------------------------------------------------------------------------------------
ivs.chunkType = 'buffer';

var curl = new CURL('http://127.0.0.1/', 'PUT');
curl.connectTimeout = 2;
curl.requestTimeout = 10;

while(!script.isInterrupted()) {
    if(!session.isReady) { break; }

    var event = ivs.getEvent();
    if(event) {
	if(event.type == "chunk-ready") {	    
    	    if(event.data.type == 'buffer') {
		url.performAsync(event.data.buffer);
	    }
        }
        if(event.type == 'curl-done') {
            consoleLog('notice', "CURL-RESPONSE: " + event.data.body);
        }
    }

    msleep(10);
}
