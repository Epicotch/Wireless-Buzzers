#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>

const int DISCOVERY_DELAY = 250;

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

const int OLED_SDA = 21;
const int OLED_SCL = 22;

int buzzed;
int pairing_index = 0;

bool console = false;

esp_now_peer_info_t buzzerConsole;

uint8_t addresses[4][6] = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
int chan = 1;

typedef struct struct_message {
	uint8_t msgType;
	uint8_t id;
	bool reset;
	bool confirm;
	uint8_t buzzer;
	char buzzerName;

} struct_message;

typedef struct struct_pairing {
	uint8_t msgType;
	uint8_t id;
	uint8_t macAddr[6];
	uint8_t channel;
} struct_pairing;

struct_message responseData;
struct_message buzzData;
struct_pairing pairingData;

enum MessageType {PAIRING, DATA,};
MessageType messagetype;

enum PairingStatus {NOT_PAIRED, PAIR_REQUEST, PAIR_REQUESTED, PAIR_PAIRED,};
PairingStatus pairingStatus = NOT_PAIRED;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {

}

void OnConnect(const uint8_t * mac_addr, const uint8_t *incomingData, int len) {
	uint8_t type = incomingData[0];
	switch(type) {
		case DATA:
			Serial.println("what huh how");
			break;
		case PAIRING:
			memcpy(&pairingData, incomingData, sizeof(pairingData));
			if (pairingData.id > 0) {
				if (pairingData.msgType == PAIRING) {
					pairingData.id = 0;
					WiFi.macAddress(pairingData.macAddr);
					pairingData.channel = chan;
					esp_err_t result = esp_now_send(mac_addr, (uint8_t *) &pairingData, sizeof(pairingData));
        			addPeer_console(mac_addr);
					memcpy(addresses[pairing_index], mac_addr, sizeof(uint8_t[6]));
					pairing_index += 1;
				}
			}
			break;
	}

}

void OnReply(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { // this will be how the local console responds to main console

}

void OnBuzz(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { // this will be how the main console responds to buzzes

}

bool addPeer_console(const uint8_t *peer_addr) {      // add pairing
  memset(&buzzerConsole, 0, sizeof(buzzerConsole));
  const esp_now_peer_info_t *peer = &buzzerConsole;
  memcpy(buzzerConsole.peer_addr, peer_addr, 6);
  
  buzzerConsole.channel = chan; // pick a channel
  buzzerConsole.encrypt = 0; // no encryption
  // check if the peer exists
  bool exists = esp_now_is_peer_exist(buzzerConsole.peer_addr);
  if (exists) {
    // buzzerConsole already paired.
    Serial.println("Already Paired");
    return true;
  }
  else {
    esp_err_t addStatus = esp_now_add_peer(peer);
    if (addStatus == ESP_OK) {
      // Pair success
      Serial.println("Pair success");
      return true;
    }
    else 
    {
      Serial.println("Pair failed");
      return false;
    }
  }
}

void addPeer_buzzer(const uint8_t * mac_addr, uint8_t chan){
  esp_now_peer_info_t peer;
  ESP_ERROR_CHECK(esp_wifi_set_channel(chan ,WIFI_SECOND_CHAN_NONE));
  esp_now_del_peer(mac_addr);
  memset(&peer, 0, sizeof(esp_now_peer_info_t));
  peer.channel = chan;
  peer.encrypt = false;
  memcpy(peer.peer_addr, mac_addr, sizeof(uint8_t[6]));
  if (esp_now_add_peer(&peer) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
  memcpy(addresses[0], mac_addr, sizeof(uint8_t[6]));
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
	if (console) { // configured as console
		esp_now_register_recv_cb(OnConnect);
		while (digitalRead(BUZZER_IN_1) || pairing_index != 4) {
		}
	}
	else { // configured as client
		// assume -1 as ID - have ID and char assigned by server
	}
}

void loop() {

}
