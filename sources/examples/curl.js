if(typeof(ivs) == 'undefined') {
    throw "Illegal runtime";
}

// ----------------------------------------------------------------------------------------------------------------------
var curl = new CURL('http://127.0.0.1/', 'GET', 10);

consoleLog('notice', "curl.url.................: " + curl.url);
consoleLog('notice', "curl.method..............: " + curl.method);
consoleLog('notice', "curl.connectTimeout......: " + curl.connectTimeout);
consoleLog('notice', "curl.requestTimeout......: " + curl.requestTimeout);
consoleLog('notice', "curl.credentials.........: " + curl.credentials);
consoleLog('notice', "curl.authType............: " + curl.authType);
consoleLog('notice', "curl.contentType.........: " + curl.contentType);
consoleLog('notice', "curl.sslVerfyPeer........: " + curl.sslVerfyPeer);
consoleLog('notice', "curl.sslVerfyHost........: " + curl.sslVerfyHost);
consoleLog('notice', "curl.sslCAcert...........: " + curl.sslCAcert);
consoleLog('notice', "curl.proxy...............: " + curl.proxy);
consoleLog('notice', "curl.proxyCredentials....: " + curl.proxyCredentials);
consoleLog('notice', "curl.proxyCAcert.........: " + curl.proxyCAcert);

var send_cnt = 0;
while(!script.isInterrupted()) {
    if(!session.isReady) { break; }

    if(send_cnt < 10) {
	/* get */
	curl.method = 'GET';
	var jid = curl.performAsync();

	/* post */
	// curl.method = 'POST';
	// var jid = curl.performAsync('POST data, 123-456\n');

	/* from */
	// curl.method = 'POST';
	//var jid = curl.performAsync({type: 'simple', name: 'field1', value: 'value1'}, {type: 'file', name: 'file', value: '/tmp/test.txt'} );
	
        consoleLog('notice', "job-id: " + jid);
        send_cnt++;
    }

    var event = ivs.getEvent();
    if(event) {
        if(event.type == 'curl-done') {
	    // consoleLog('notice', "CURL-RESPONSE: " + JSON.stringify(event));
            consoleLog('notice', "CURL-RESPONSE: " + event.data.body);
        }
    }

    msleep(10);
}
