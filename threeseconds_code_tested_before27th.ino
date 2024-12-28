//2 sec
#define DEBUG false

#define LTE_PWRKEY_PIN 5
#define LTE_RESET_PIN 6
#define LTE_FLIGHT_PIN 7

const int switch1Pin = 8;
const int switch2Pin = 9;

volatile bool sosPressed = false;
volatile bool okPressed = false;
volatile bool sendingMessage = false;

unsigned long lastTrackSendTime = 0;
const unsigned long trackSendInterval = 500; // 500 milliseconds

int gpsSendAttempts = 0;
const int maxAttempts = 25;

String sendData(String command, const int timeout, boolean debug = false) {
    String response = "";
    Serial1.println(command);

    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            response += c;
        }
        // Optionally, perform other tasks or checks here
    }

    if (debug) {
        SerialUSB.print(command);
        SerialUSB.print(" Response: ");
        SerialUSB.println(response);
    }

    return response;
}

String extractNMEA(String response) {
    int start = response.indexOf("+CGPSINFO: ");
    if (start != -1) {
        start += 11; // Move past "+CGPSINFO: "
        int end = response.indexOf("OK", start);
        if (end != -1 && end > start) {
            String nmea = response.substring(start, end);
            nmea.trim(); // Trim any leading/trailing whitespace
            return nmea;
        }
    }
    return ""; // Return empty string if NMEA sentence not found or invalid format
}

bool sendHTTPRequest(String url, String jsonPayload) {
    SerialUSB.println("Sending HTTP request to " + url);

    sendData("AT+HTTPINIT", 100, DEBUG);
    sendData("AT+HTTPPARA=\"CID\",1", 100, DEBUG);
    sendData("AT+HTTPPARA=\"URL\",\"" + url + "\"", 100, DEBUG);
    sendData("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 100, DEBUG);
    sendData("AT+HTTPDATA=" + String(jsonPayload.length()) + ",1000", 100);  // Reduced timeout for HTTPDATA
    sendData(jsonPayload, 100, DEBUG);
    String response = sendData("AT+HTTPACTION=1", 1000, DEBUG);  // Reduced timeout for HTTPACTION
    sendData("AT+HTTPTERM", 100, DEBUG);

    if (response.indexOf("200") != -1) {
        SerialUSB.println("HTTP request sent successfully.");
        return false;
    } else {
        SerialUSB.println("Failed to send HTTP request.");
        return true;
    }
}

void sendTrackData() {
    String gpsInfo = sendData("AT+CGPSINFO", 1000, DEBUG);  // Reduced timeout for GPS info
    String nmeaSentence = extractNMEA(gpsInfo);
    SerialUSB.println(nmeaSentence + " nmea");

    if (nmeaSentence.length() > 8) {
        SerialUSB.println("Sending Track data");

        String jsonPayload = "{\"nmea\": \"" + nmeaSentence + "\",\"carId\":2}";
        String requestBinURL = "https://blueband-speed-zr7gm6w4cq-el.a.run.app/track";
        if (sendHTTPRequest(requestBinURL, jsonPayload)) {
            // Handle failure if needed
        }
    }
}

void handleSosPress() {
    if (digitalRead(switch1Pin) == LOW) {  // Check if the button is still pressed
        sosPressed = true;
    }
}

void handleOkPress() {
    if (digitalRead(switch2Pin) == LOW) {  // Check if the button is still pressed
        okPressed = true;
    }
}

void setup() {
    SerialUSB.begin(115200);
    Serial1.begin(115200);

    pinMode(LTE_RESET_PIN, OUTPUT);
    digitalWrite(LTE_RESET_PIN, LOW);

    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);

    pinMode(LTE_FLIGHT_PIN, OUTPUT);
    digitalWrite(LTE_FLIGHT_PIN, LOW);

    pinMode(switch1Pin, INPUT_PULLUP);  // Enable internal pull-up resistor
    pinMode(switch2Pin, INPUT_PULLUP);  // Enable internal pull-up resistor

    attachInterrupt(digitalPinToInterrupt(switch1Pin), handleSosPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(switch2Pin), handleOkPress, FALLING);

    while (!SerialUSB) {
        delay(10);
    }

    sosPressed = false;
    okPressed = false;

    SerialUSB.println("Switch test initialized.");
    String response = sendData("AT+CGATT=0", 3000, DEBUG);
    delay(1000);
    response = sendData("AT+CGATT=1", 3000, DEBUG);
    delay(1000);
    response = sendData("AT+CGACT=1,1", 3000, DEBUG);
    delay(1000);
    response = sendData("AT+CGPADDR=1", 3000, DEBUG);
    
    if (response.indexOf("OK") != -1 && response.indexOf(".") != -1) {
        SerialUSB.println("Internet connected.");
    } else {
        SerialUSB.println("Internet not connected. Response: " + response);
    }
  
    response = sendData("AT+CGPS=0", 3000, DEBUG);
    response = sendData("AT+CGPS=1", 3000, DEBUG);
}

void loop() {
    static unsigned long lastCheckTime = 0;
    unsigned long currentMillis = millis();
    
    if (sosPressed) {
        sendingMessage = true;
        sosPressed = false;
        int max_sos_attempt = 0;
        while (max_sos_attempt++ < 25) {
            if (!sendHTTPRequest("https://blueband-speed-zr7gm6w4cq-el.a.run.app/sos", "{\"carId\":2, \"message\": \"SOS\"}")) {
                SerialUSB.println("SOS DONE");
                break;
            }
            SerialUSB.println("SOS NOT DONE");
        }
        sendingMessage = false;
    }

    if (okPressed) {
        sendingMessage = true;
        okPressed = false;
        int max_ok_attempt = 0;
        while (max_ok_attempt++ < 25) {
            if (!sendHTTPRequest("https://blueband-speed-zr7gm6w4cq-el.a.run.app/ok", "{\"carId\":2, \"message\": \"OK\"}")) {
                SerialUSB.println("OK DONE");
                break;
            }
            SerialUSB.println("OK NOT DONE");
        }
        sendingMessage = false;
    }

    // Check for GPS data to send every 500 ms
    if (!sendingMessage && (currentMillis - lastTrackSendTime >= trackSendInterval)) {
        sendTrackData();  // Attempt to send track data only if no SOS or OK is being sent
        lastTrackSendTime = currentMillis; // Update the last check time
    }
}
