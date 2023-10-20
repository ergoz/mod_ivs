if(typeof(ivs) == 'undefined') {
    throw "Illegal runtime";
}

// ----------------------------------------------------------------------------------------------------------------------
var chatGPT = new ChatGPT("---your-api-key---");
chatGPT.connectTimeout = 3;  // sec
chatGPT.requestTimeout = 10; // sec
chatGPT.logHttpErrors  = true;

ivs.language = 'en';
ivs.ttsEngine = 'google';
ivs.chunkType = 'file';
ivs.chunkEncoding = 'mp3';


consoleLog('notice', "ivs.language..............: " + ivs.language);
consoleLog('notice', "ivs.ttsEngine.............: " + ivs.ttsEngine);
consoleLog('notice', "ivs.chunkType.............: " + ivs.chunkType);
consoleLog('notice', "ivs.chunkEncoding.........: " + ivs.chunkEncoding);
consoleLog('notice', "chatGPT.chatModel.........: " + chatGPT.chatModel);
consoleLog('notice', "chatGPT.whisperModel......: " + chatGPT.whisperModel);


var fl_play_hello = true;

while(!script.isInterrupted()) {
    if(!session.isReady) { break; }

    if(fl_play_hello) {
        ivs.say("Hello, how can I help you?");
        fl_play_hello = false;
    }

    var event = ivs.getEvent();
    if(event) {
        consoleLog('notice', "IVS-EVENT: " + JSON.stringify(event));

        if(event.type == "chunk-ready") {
            //ivs.playback(event.data.file, true, true);        
            chatGPT.aksWhisper(event.data.file, true, true);
        }

        if(event.type == "transcription-done") {
            if(event.data.text && event.data.text.length >= 2) {
                chatGPT.askChatGPT(event.data.text, true);
            }
        }

        if(event.type == "nlp-done") {
            if(event.data.text && event.data.text.length >= 2) {
                ivs.say(event.data.text, true);
            }
        }
    }

    msleep(10);
}