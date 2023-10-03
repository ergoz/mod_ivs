//
// A simple voice chat via OpenAI api
//
var chatGPT = new ChatGPT("---your-api-key---");
chatGPT.connectTimeout = 3;  // sec
chatGPT.requestTimeout = 15; // sec
chatGPT.logHttpErrors  = true;

// ivs props
ivs.ttsEngine = 'google';
ivs.language = 'en';

//ivs.mohTime = 5;
//ivs.mohMessage = "Your request is still processing, please hold on the line.";

// --------------------------------------------------------------------------------
consoleLog('notice', "Context language..........: " + ivs.language);
consoleLog('notice', "TTS engine................: " + ivs.ttsEngine);
consoleLog('notice', "chatGPT.chatModel.........: " + chatGPT.chatModel);
consoleLog('notice', "chatGPT.whisperModel......: " + chatGPT.whisperModel);

// ---------------------------------------------------------------------------------
var fl_play_hello = false;
var transcripion_proc = 0;
var nlp_proc = 0;

while(!script.isInterrupted()) {
    if(!session.isReady) { break; }

    if(!fl_play_hello) {
        ivs.say("Hello, I'm AI and ready to answer your questions.");
        ivs.say("What can I help you?");
        fl_play_hello = true;
    }

    var event = ivs.getEvent();
    if(event) {
        consoleLog('notice', "IVS-EVENT: " + JSON.stringify(event));

        if(event.type == "chunk-ready") {
            // ivs.playback(event.file, true, true);    

            var jid = chatGPT.aksWhisper(event.file, true, true);
            if(jid) { transcripion_proc++; }
        }

        if(event.type == "transcription-done") {
            if(transcripion_proc > 0) { transcripion_proc--; }

            // event.confidence >= 30.0
            if(event.text && event.text.length >= 2) {
                var jid = chatGPT.askChatGPT(event.text, true);
                if(jid) { nlp_proc++; }
            }
        }

        if(event.type == "nlp-done") {
            if(nlp_proc > 0) { nlp_proc--; }
            if(event.text && event.text.length >= 2) {
                ivs.say(event.text, true);
            }
        }
    }

    msleep(10);
}



