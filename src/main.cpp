#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>
#include <Wire.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const long BUZZ_LENGTH = 2000L;
const long DISCOVERY_TIMEOUT = 600L;

const int MODE_PIN = 35;
const int BUZZER_PIN = 25;
const int BUZZER_IN_1 = 23;
const int BUZZER_LED_1 = 19;
const int BUZZER_IN_2 = 18;
const int BUZZER_LED_2 = 17;
const int BUZZER_IN_3 = 16;
const int BUZZER_LED_3 = 15;
const int BUZZER_IN_4 = 14;
const int BUZZER_LED_4 = 13;

const int bzLEDs[4] = {BUZZER_LED_1, BUZZER_LED_2, BUZZER_LED_3, BUZZER_LED_4};

const int OLED_SDA = 21;
const int OLED_SCL = 22;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;

const int OLED_RESET = -1;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long timerStopTime = (long) 0;

uint8_t buzzed = 100;
uint8_t pairing_index = 0;
int found = 0;
bool match = false;
char id_letter;
uint8_t id;
bool resetting = false;

bool console = false;

bool resetPrev = false;

esp_now_peer_info_t buzzerConsole;
uint8_t addresses[6][6] = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
char letters[6] = {'A', 'B', 'C', 'D', 'E', 'F'};
int chan = 4;

typedef struct struct_message {
	uint8_t msgType;
	uint8_t id;
	uint8_t buzzer;
	uint8_t macAddr[6];
} struct_message;

typedef struct struct_pairing {
	uint8_t msgType;
	uint8_t id;
	uint8_t macAddr[6];
	uint8_t channel;
	uint8_t assigned_id;
} struct_pairing;

struct_message responseData;
struct_message buzzData;
struct_pairing pairingData;

enum MessageType {PAIRING, DATA, REDO, CONFIRM,};
MessageType messagetype;
enum PairingStatus {NOT_PAIRED, PAIR_REQUEST, PAIR_REQUESTED, PAIR_PAIRED,};
PairingStatus pairingStatus = NOT_PAIRED;

enum BuzzStates {NOT_BUZZED, BUZZ_PENDING, BUZZ_SUCCESSFUL, BUZZ_HOLDING, RESET,};
BuzzStates buzzState = NOT_BUZZED;

void(* resetFunc) (void) = 0; //declare reset function @ address 0


void addPeer_buzzer(const uint8_t * mac_addr, uint8_t chan) {
	esp_now_peer_info_t peer;
	esp_now_del_peer(mac_addr);
	memset(&peer, 0, sizeof(esp_now_peer_info_t));
	peer.channel = chan;
	peer.encrypt = 0;
	memcpy(peer.peer_addr, mac_addr, sizeof(uint8_t[6]));
	if (esp_now_add_peer(&peer) != ESP_OK) {
		Serial.println("Failed to add peer");
		return;
	}
	memcpy(addresses[0], mac_addr, sizeof(uint8_t[6]));
}

bool addPeer_console(const uint8_t *peer_addr) {
	memset(&buzzerConsole, 0, sizeof(buzzerConsole));
	const esp_now_peer_info_t *peer = &buzzerConsole;
	memcpy(buzzerConsole.peer_addr, peer_addr, 6);
	memcpy(addresses[pairing_index], peer_addr, 6);

	buzzerConsole.channel = chan;
	buzzerConsole.encrypt = 0;

	bool exists = esp_now_is_peer_exist(buzzerConsole.peer_addr);
	if (exists) {
		return true;
	}
	else {
		esp_err_t pairStatus = esp_now_add_peer(peer);
		if (pairStatus == ESP_OK) {
			display.println("Connected " + letters[pairing_index]);
			display.display();
			return true;
		}
		else {
			Serial.println(esp_err_to_name(pairStatus));
			return false;
		}
	}
}

void printMAC(const uint8_t * mac_addr){
	char macStr[18];
	snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	Serial.print(macStr);
}

void consolePairing(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	Serial.println("Recieved ping");
	switch (type) {
		case PAIRING: // pairing confirmed
			Serial.println("Type pairing");
			memcpy(&pairingData, incomingData, sizeof(pairingData));
			Serial.println("ID = " + pairingData.id);
			if (pairingData.id == 100) {
				Serial.print("Sending pairing response to ");
				printMAC(mac_addr);
				Serial.println("\n");
				pairingData.id = 0;
				WiFi.macAddress(pairingData.macAddr);
				pairingData.channel = chan;
				addPeer_console(mac_addr);
				pairingData.assigned_id = ++pairing_index;
				Serial.println(esp_err_to_name(esp_now_send(mac_addr, (uint8_t *) &pairingData, sizeof(pairingData))));
				Serial.print("Current pairing index: ");
				Serial.println(pairing_index);
			}
			break;
		case DATA:
			Serial.println("huh");
			break;
	}
}

void buzzerPairing(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	Serial.println("Recieved response");
	switch (type) {
		case PAIRING:
			Serial.println("Type pairing");
			memcpy(&pairingData, incomingData, sizeof(pairingData));
			if (pairingData.id == 0) {
				Serial.println("ID 0");
				id_letter = letters[pairingData.assigned_id - 1];
				id = pairingData.assigned_id;
				chan = pairingData.channel;
				addPeer_buzzer(mac_addr, pairingData.channel);
				pairingStatus = PAIR_PAIRED;
				Serial.print("Assigned");
				Serial.println(id_letter);
			}
			break;
		case DATA:
			Serial.println("huh");
			break;
	}
}

void onBuzzResponse(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	Serial.println("Buzzer ping");
	switch (type) {
		case DATA:
			break;
		case REDO:
			Serial.println("Type reset");
			memcpy(&responseData, incomingData, sizeof(incomingData));
			if (responseData.id == 0) {
				Serial.println("From console");
				buzzState = RESET;
			}
			break;
		case CONFIRM:
			Serial.println("Type confirm");
			Serial.println(responseData.buzzer);
			memcpy(&responseData, incomingData, sizeof(incomingData));
			if (responseData.id == 0) {
				Serial.println("Successful buzz");
				timerStopTime = millis() + BUZZ_LENGTH;
				buzzState = BUZZ_SUCCESSFUL;
			}
			break;
		case PAIRING:
			Serial.println("hm");
			break;
	}
}

void onBuzz(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	Serial.println("Recieved onBuzz ping");
	switch (type) {
		case DATA:
			memcpy(&responseData, incomingData, sizeof(incomingData));
			Serial.println("Data!");
			match = false;
			for (int i = 0; i < 4; i++) {
				found = 0;
				for (int j = 0; j < 6; j++) {
					Serial.printf("Comparing %02x to %02x\n", mac_addr[j], addresses[i][j]);
					if (mac_addr[j] == addresses[i][j]) {
						found++;
					}
					if (buzzState == RESET) {
						break;
					}
				}
				if (buzzState == RESET) {
					break;
				}
				if (found == 6) {
					match = true;
					break;
				}
				Serial.print("Matched ");
				Serial.println(found);
			}
			if (buzzState == RESET) {
				break;
			}
			if (!match) {
				Serial.println("No match");
				break;
			}
			Serial.println("MAC address match");
			if (responseData.id != 0) {
				Serial.println("From buzzer!");
				if (buzzState == NOT_BUZZED && digitalRead(BUZZER_IN_1)) {
					Serial.println("Good buzz");
					display.setCursor(44, 18);
					display.setTextSize(5);
					display.setTextColor(WHITE);
					Serial.print(letters[responseData.id - 1]);
					Serial.println(responseData.buzzer + 1);
					display.print(letters[responseData.id - 1]);
					display.print(responseData.buzzer + 1);
					display.display();
					buzzed = responseData.id;
					buzzData.id = 0;
					buzzData.msgType = CONFIRM;
					buzzData.buzzer = responseData.buzzer;
					WiFi.macAddress(buzzData.macAddr);
					Serial.println(esp_err_to_name(esp_now_send(mac_addr, (uint8_t *) &buzzData, sizeof(buzzData))));
					buzzState = BUZZ_SUCCESSFUL;
					}
				else {
					buzzData.id = 0;
					buzzData.msgType = REDO;
					buzzData.buzzer = responseData.buzzer;
					esp_now_send(mac_addr, (uint8_t *) &buzzData, sizeof(buzzData));
				}
			}
			break;
		case PAIRING:
			break;
	}
}

void buzzStateMachine_buzzer() {
	switch (buzzState) {
		case NOT_BUZZED:
			break;
		case BUZZ_PENDING:
			buzzData.buzzer = buzzed;
			buzzData.msgType = DATA;
			WiFi.macAddress(buzzData.macAddr);
			Serial.printf("This MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", buzzData.macAddr[0], buzzData.macAddr[1], buzzData.macAddr[2], buzzData.macAddr[3], buzzData.macAddr[4], buzzData.macAddr[5]);
			buzzData.id = id;
			Serial.print("Buzz on ");
			Serial.print(letters[id - 1]);
			Serial.println(buzzData.buzzer + 1);
			Serial.println(esp_err_to_name(esp_now_send(addresses[0], (uint8_t *) &buzzData, sizeof(buzzData))));
			buzzState = NOT_BUZZED;
			break;
		case BUZZ_SUCCESSFUL:
			digitalWrite(BUZZER_PIN, 1);
			digitalWrite(bzLEDs[buzzed], 1);
			if (millis() > timerStopTime) {
				buzzState = BUZZ_HOLDING;
			}
			break;
		case BUZZ_HOLDING:
			digitalWrite(BUZZER_PIN, 0);
			break;
		case RESET:
			Serial.println("Resetting!");
			digitalWrite(BUZZER_PIN, 0);
			digitalWrite(BUZZER_LED_1, 0);
			digitalWrite(BUZZER_LED_2, 0);
			digitalWrite(BUZZER_LED_3, 0);
			digitalWrite(BUZZER_LED_4, 0);
			buzzed = 100;
			buzzState = NOT_BUZZED;
			break;
	}
}

void buzzStateMachine_console() {
	switch (buzzState) {
		case NOT_BUZZED:
			break;
		case BUZZ_PENDING:
			break;
		case BUZZ_SUCCESSFUL:
			break;
		case BUZZ_HOLDING:
			break;
		case RESET:
			if (resetting) break;
			Serial.println("Resetting");
			buzzed = 100;
			buzzData.id = 0;
			buzzData.msgType = REDO;
			buzzData.buzzer = 100;
			resetting = true;
			for (int i; i < pairing_index; i++) {
				Serial.print("Resetting buzzer ");
				Serial.print(i + 1);
				Serial.print(" at ");
				Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n", addresses[i][0], addresses[i][1], addresses[i][2], addresses[i][3], addresses[i][4], addresses[i][5]);
				noInterrupts();
				Serial.println(esp_err_to_name(esp_now_send(addresses[i], (uint8_t *) &buzzData, sizeof(buzzData))));
				interrupts();
			}
			resetting = false;
			buzzState = NOT_BUZZED;
			display.clearDisplay();
			display.display();
			break;
	}
}

void setup() {
	Serial.begin(115200);
	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
		Serial.println("Failed to start OLED");
		for(;;);
	}
	display.setTextColor(WHITE);
	display.clearDisplay();
	display.setTextSize(2);
	display.setCursor(0, 5);
	display.display();
	pinMode(MODE_PIN, INPUT);
	pinMode(BUZZER_PIN, OUTPUT);
	pinMode(BUZZER_IN_1, INPUT);
	pinMode(BUZZER_LED_1, OUTPUT);
	pinMode(BUZZER_IN_2, INPUT);
	pinMode(BUZZER_LED_2, OUTPUT);
	pinMode(BUZZER_IN_3, INPUT);
	pinMode(BUZZER_LED_3, OUTPUT);
	pinMode(BUZZER_IN_4, INPUT);
	pinMode(BUZZER_LED_4, OUTPUT);
	console = !digitalRead(MODE_PIN);

	WiFi.mode(WIFI_STA);
	if (esp_now_init() != ESP_OK) {
		Serial.println("Error initializing ESP-NOW");
		display.setTextSize(3);
		display.println("ESP-NOW failed!");
		display.display();
		return;
	}
	Serial.println(esp_err_to_name(esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE)));
	display.clearDisplay();
	if (console) {
		//display.setCursor(5, 5);
		//display.setTextSize(3);
		Serial.println("Console mode");
		display.println("Console\nmode");
		display.display();
		Serial.println(esp_err_to_name(esp_now_register_recv_cb(consolePairing)));
		while (digitalRead(BUZZER_IN_1) && !(pairing_index == 6)) {}
		Serial.println("Starting game cycle");
		Serial.println(esp_err_to_name(esp_now_unregister_recv_cb()));
		Serial.println(esp_err_to_name(esp_now_register_recv_cb(onBuzz)));
		display.setTextSize(5);
		display.clearDisplay();
	}
	else {
		Serial.println("Buzzer mode");
		display.println("Buzzer\nmode");
		id = 100;
		display.display();
		Serial.println(esp_err_to_name(esp_now_register_recv_cb(buzzerPairing)));
		addPeer_buzzer(addresses[0], chan);
		pairingData.msgType = PAIRING;
		pairingData.id = id;
		pairingData.channel = chan;
		Serial.println(esp_err_to_name(esp_now_send(addresses[0], (uint8_t *) &pairingData, sizeof(pairingData))));
		timerStopTime = DISCOVERY_TIMEOUT + millis();
		pairingStatus = PAIR_REQUESTED;
		while (pairingStatus != PAIR_PAIRED) {
			if (millis() > timerStopTime) {
				// could not find anything
				Serial.println("Unable to find console, retrying...");
				resetFunc();
			}
		}
		Serial.println("Found console!");
		display.println("Connected!");
		display.display();
		Serial.println(esp_err_to_name(esp_now_unregister_recv_cb()));
		Serial.println(esp_err_to_name(esp_now_register_recv_cb(onBuzzResponse)));
		display.setTextSize(5);
		display.clearDisplay();
		display.setCursor(54, 18);
		display.print(letters[id - 1]);
	}
	display.display();
	buzzState = NOT_BUZZED;
}

void loop() {
	//Serial.println("loop weeeeeeeee");
	if (!console) {
		buzzStateMachine_buzzer();
		if (buzzed == 100) {
			if (!digitalRead(BUZZER_IN_1)) {
				buzzed = 0;
			}
			else if (!digitalRead(BUZZER_IN_2)) {
				buzzed = 1;
			}
			else if (!digitalRead(BUZZER_IN_3)) {
				buzzed = 2;
			} 
			else if (!digitalRead(BUZZER_IN_4)) {
				buzzed = 3;
			}
			if (buzzed != 100) {
				buzzState = BUZZ_PENDING;
			}
			Serial.print(!digitalRead(BUZZER_IN_1));
			Serial.print(!digitalRead(BUZZER_IN_2));
			Serial.print(!digitalRead(BUZZER_IN_3));
			Serial.println(!digitalRead(BUZZER_IN_4));
			Serial.print("Current buzz: ");
			Serial.println(buzzed);
		}
	}
	if (console) {
		// do the console logic in here
		buzzStateMachine_console();
		if (!digitalRead(BUZZER_IN_1) && resetPrev) {
			buzzState = RESET;
		}
		resetPrev = digitalRead(BUZZER_IN_1);
	}
}
