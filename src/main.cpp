#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>
#include <Wire.h>

const long BUZZ_LENGTH = 2000L;
const long DISCOVERY_TIMEOUT = 3000L;

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

unsigned long timerStopTime = (long) 0;

int buzzed = -1;
int pairing_index = 0;
char id_letter;
int id;

bool lockdown = false;

bool console = false;

esp_now_peer_info_t buzzerConsole;

uint8_t addresses[4][6] = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
char letters[] = {'A', 'B', 'C', 'D'};
int chan = 1;

typedef struct struct_message {
	uint8_t msgType;
	uint8_t id;
	bool reset;
	bool confirm;
	uint8_t buzzer;
	char console;
} struct_message;

typedef struct struct_pairing {
	uint8_t msgType;
	uint8_t id;
	uint8_t macAddr[6];
	uint8_t channel;
	uint8_t assigned_id;
	char assigned_letter;
} struct_pairing;

struct_message responseData;
struct_message buzzData;
struct_pairing pairingData;

enum MessageType {PAIRING, DATA,};
MessageType messagetype;

enum PairingStatus {NOT_PAIRED, PAIR_REQUEST, PAIR_REQUESTED, PAIR_PAIRED,};
PairingStatus pairingStatus = NOT_PAIRED;

enum BuzzStates {NOT_BUZZED, BUZZ_PENDING, BUZZ_SUCCESSFUL, BUZZ_HOLDING, RESET,};
BuzzStates buzzState = NOT_BUZZED;

void(* resetFunc) (void) = 0; //declare reset function @ address 0


void addPeer_buzzer(const uint8_t * mac_addr, uint8_t chan) {
	esp_now_peer_info_t peer;
	ESP_ERROR_CHECK(esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE));
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
	memcpy(addresses[pairing_index], buzzerConsole.peer_addr, 6);

	buzzerConsole.channel = chan;
	buzzerConsole.encrypt = 0;

	bool exists = esp_now_is_peer_exist(buzzerConsole.peer_addr);
	if (exists) {
		return true;
	}
	else {
		esp_err_t pairStatus = esp_now_add_peer(peer);
		if (pairStatus == ESP_OK) {
			return true;
		}
		else {
			return false;
		}
	}
}

void consolePairing(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { // do something similar for the buzzers
	uint8_t type = incomingData[0];
	switch (type) {
		case PAIRING: // pairing confirmed
			memcpy(&pairingData, incomingData, sizeof(pairingData));
			if (pairingData.id == -1) {
				if (pairingData.msgType == PAIRING) {
					pairingData.id = 0;
					WiFi.macAddress(pairingData.macAddr);
					pairingData.channel = chan;
					pairingData.assigned_letter = letters[pairing_index];
					pairingData.assigned_id = ++pairing_index;
					esp_err_t result = esp_now_send(mac_addr, (uint8_t *) &pairingData, sizeof(pairingData));
					addPeer_console(mac_addr);
				}
			}
			break;
		case DATA:
			Serial.println("huh");
			break;
	}
}

void buzzerPairing(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	switch (type) {
		case PAIRING:
			memcpy(&pairingData, incomingData, sizeof(pairingData));
			if (pairingData.id == 0) {
				if (pairingData.msgType == PAIRING) {
					id_letter = pairingData.assigned_letter;
					id = pairingData.assigned_id;
					chan = pairingData.channel;
					addPeer_buzzer(pairingData.macAddr, pairingData.channel);
					pairingStatus = PAIR_PAIRED;
				}
			}
			break;
		case DATA:
			Serial.println("huh");
			break;
	}
}

void onBuzzResponse(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	switch (type) {
		case DATA:
			memcpy(&responseData, incomingData, sizeof(incomingData));
			if (responseData.id == 0 && responseData.console == id_letter) {
				if(responseData.confirm) {
					timerStopTime = millis() + BUZZ_LENGTH;
					buzzState = BUZZ_SUCCESSFUL;
				}
				else if(responseData.reset) {
					buzzState = RESET;
				}
			}
			break;
		case PAIRING:
			Serial.println("what the fu");
			break;
	}
}

void onBuzz(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	int found = 0;
	for (int i; i < 4; ++i) {
		for (int j; j < 6; ++i) {
			if (mac_addr[j] == addresses[i][j]) {
				found++;
			}
		}
	}
	if (found != 6) {
		return;
	}
	switch (type) {
		case DATA:
			memcpy(&responseData, incomingData, sizeof(incomingData));
			if (responseData.id != 0) {
				if (buzzState != NOT_BUZZED) {
					buzzed = responseData.id;
					buzzData.id = 0;
					buzzData.msgType = DATA;
					buzzData.console = 'E';
					buzzData.confirm = true;
					buzzData.reset = false;
					buzzData.buzzer = responseData.id;
					esp_now_send(mac_addr, (uint8_t *) &buzzData, sizeof(buzzData));
					buzzState = BUZZ_SUCCESSFUL;
					}
				else {
					buzzData.id = 0;
					buzzData.msgType = DATA;
					buzzData.console = 'E';
					buzzData.confirm = false;
					buzzData.reset = true;
					buzzData.buzzer = responseData.id;
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
			buzzData.console = id_letter;
			buzzData.id = id;
			esp_now_send(addresses[0], (uint8_t *) &buzzData, sizeof(buzzData));
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
		case RESET:
			digitalWrite(BUZZER_PIN, 0);
			digitalWrite(BUZZER_LED_1, 0);
			digitalWrite(BUZZER_LED_2, 0);
			digitalWrite(BUZZER_LED_3, 0);
			digitalWrite(BUZZER_LED_4, 0);
			buzzed = -1;
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
			buzzed = -1;
			buzzData.id = 0;
			buzzData.console = 'E';
			buzzData.confirm = false;
			buzzData.reset = true;
			buzzData.msgType = DATA;
			buzzData.buzzer = -1;
			for (int i; i < 4; ++i) {
				esp_now_send(addresses[i], (uint8_t *) &buzzData, sizeof(buzzData));
			}
			buzzState = NOT_BUZZED;
	}
}

void setup() {
	
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

	Serial.begin(115200);
	WiFi.mode(WIFI_STA);
	if (esp_now_init() != ESP_OK) {
		Serial.println("Error initializing ESP-NOW");
		return;
	}
	if (console) {
		esp_now_register_recv_cb(consolePairing);
		while (digitalRead(BUZZER_IN_1) || pairing_index != 4) {
		}
		esp_now_unregister_recv_cb();
		esp_now_register_recv_cb(onBuzz);
	}
	else {
		esp_now_register_recv_cb(buzzerPairing);
		pairingData.msgType = PAIRING;
		pairingData.id = id;
		pairingData.channel = chan;
		esp_now_send(addresses[0], (uint8_t *) &pairingData, sizeof(pairingData));
		timerStopTime = DISCOVERY_TIMEOUT + millis();
		while (pairingStatus != PAIR_PAIRED) {
			if (millis() > timerStopTime) {
				// could not find anything
				resetFunc();
			}
			pairingStatus = PAIR_REQUESTED;
		}
		esp_now_unregister_recv_cb();
		esp_now_register_recv_cb(onBuzzResponse);
	}
}

void loop() {
	if (!console) {
		buzzStateMachine_buzzer();
		if (buzzed == -1) {
			if (digitalRead(BUZZER_IN_1)) {
				buzzed = 0;
			}
			else if (digitalRead(BUZZER_IN_2)) {
				buzzed = 1;
			}
			else if (digitalRead(BUZZER_IN_3)) {
				buzzed = 2;
			} 
			else if (digitalRead(BUZZER_IN_4)) {
				buzzed = 3;
			}
			if (buzzed != -1) {
				buzzState = BUZZ_PENDING;
			}
		}
	}
	if (console) {
		// do the console logic in here
		if (digitalRead(BUZZER_IN_1)) {
			buzzState = RESET;
		}
	}
}
