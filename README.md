# mod_ivs
todo...


### dialplan
```xml
<extension name="ivs-demo">
  <condition field="destination_number" expression="^(3111)$">
    <action application="answer"/>
    <action application="ivs" data="chatgpt.js"/>
    <action application="hangup"/>
  </condition>
</extension>
```

### chatgpt.js
```js
var chatGPT = new ChatGPT("---your-api-key---");
chatGPT.connectTimeout = 3;  // sec
chatGPT.requestTimeout = 15; // sec
chatGPT.logHttpErrors  = true;

ivs.ttsEngine = 'google';
ivs.language = 'en';

consoleLog('notice', "Context language..........: " + ivs.language);
consoleLog('notice', "TTS engine................: " + ivs.ttsEngine);
consoleLog('notice', "chatGPT.chatModel.........: " + chatGPT.chatModel);
consoleLog('notice', "chatGPT.whisperModel......: " + chatGPT.whisperModel);

var fl_play_hello = false;

while(!script.isInterrupted()) {
    if(!session.isReady) { break; }

    if(!fl_play_hello) {
        ivs.say("Hello, I'm AI and ready to answer your questions.");
        ivs.say("What can I help you?");
        fl_play_hello = true;
    }

    var event = ivs.getEvent();
    if(event) {
	// consoleLog('notice', "IVS-EVENT: " + JSON.stringify(event));

        if(event.type == "chunk-ready") {
    	    chatGPT.aksWhisper(event.file, true, true);
        }

        if(event.type == "transcription-done") {
            if(event.text && event.text.length >= 2) {
        	chatGPT.askChatGPT(event.text, true);
            }
        }

        if(event.type == "nlp-done") {
            if(event.text && event.text.length >= 2) {
                ivs.say(event.text, true);
            }
        }
    }

    msleep(10);
}

```
