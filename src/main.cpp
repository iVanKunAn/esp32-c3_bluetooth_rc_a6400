#include <Arduino.h>
#include <BLEDevice.h>

// Please enter your camera bluetooth name. My Sony a6400 name is:
#define bleServerName "ILCE-6400"
#define SHOOT_BUTTON 1

int8_t take_serial = 0;
int8_t take_shoot = 0;
int8_t was_pressed = 0;
int8_t was_released = 0;
int8_t fully_up_send = 0;
int8_t fully_down_send = 0;
int8_t first_press = 0;
uint8_t Shutter_Half_Down[] = {0x01, 0x07};
uint8_t Shutter_Fully_Down[] = {0x01, 0x09};
uint8_t Shutter_Half_Up[] = {0x01, 0x06};
uint8_t Shutter_Fully_Up[] = {0x01, 0x08};

static BLEAddress *pServerAddress;
static boolean doPairing = false;
static boolean doConnect = false;
static boolean connected = false;
BLERemoteCharacteristic *remoteCommand;
BLERemoteCharacteristic *remoteNotify;

unsigned long previousMillis = 0;
unsigned long currentMillis = millis();
const long time_full_dwn = 600;
const long time_full_up = 100;

// helper function for print debug
void printHex(uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; i++)
	{
		if (data[i] < 0x10)
		{
			Serial.print("0x0");
		}
		else
		{
			Serial.print("0x");
		}
		Serial.print(data[i], HEX);
		if (i < length - 1)
		{
			Serial.print(" ");
		}
	}
}

static void commandNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
											 uint8_t *pData, size_t length, bool isNotify)
{
	Serial.print("Received from command channel: ");
	printHex(pData, length);
	if (isNotify)
	{
		Serial.println(" - notify");
	}
	else
	{
		Serial.println(" - not notify");
	}
}

static void notifyNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
											uint8_t *pData, size_t length, bool isNotify)
{
	Serial.print("Received from notify channel: ");
	printHex(pData, length);
	if (isNotify)
	{
		Serial.println(" - notify");
	}
	else
	{
		Serial.println(" - not notify");
	}
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		Serial.print("BLE: something found: ");
		Serial.println(advertisedDevice.getName().c_str());
		if (advertisedDevice.getName() == bleServerName)
		{																						 // Check if the name of the advertiser matches
			advertisedDevice.getScan()->stop();										 // Scan can be stopped, we found what we are looking for
			pServerAddress = new BLEAddress(advertisedDevice.getAddress()); // Address of advertiser is the one we need
			Serial.println("Camera found. Connecting!");
			Serial.print("Payload: ");
			printHex(advertisedDevice.getPayload(), advertisedDevice.getPayloadLength());
			Serial.println("");
			auto data = advertisedDevice.getPayload();
			for (size_t i = 1; i < advertisedDevice.getPayloadLength(); i++)
			{
				if (data[i - 1] == 0x22)
				{
					if ((data[i] & 0x40) == 0x40 && (data[i] & 0x02) == 0x02)
					{
						Serial.println("Camera is ready to paring");
						doPairing = true;
					}
					else
					{
						doConnect = true;
						Serial.println("Camera is not ready to paring, but try to connect");
					}
				}
			}
		}
	}
};

class MyClientCallback : public BLEClientCallbacks
{
	void onConnect(BLEClient *pclient)
	{
		Serial.println("Connected");
	}

	void onDisconnect(BLEClient *pclient)
	{
		connected = false;
		Serial.println("Disconnected");
	}
};

// Conn
bool connectToServer(BLEAddress pAddress)
{
	BLEClient *pClient = BLEDevice::createClient();
	pClient->setClientCallbacks(new MyClientCallback());

	// Connect to the remove BLE Server.
	if (pClient->connect(pAddress))
	{
		Serial.println(" - Connected to server");
		doPairing = false;
		doConnect = false;

		BLERemoteService *pRemoteService = pClient->getService("8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF");
		if (pRemoteService == nullptr)
		{
			Serial.print("Failed to find our service UUID");
			return false;
		}

		remoteCommand = pRemoteService->getCharacteristic(BLEUUID((uint16_t)0xFF01));
		remoteNotify = pRemoteService->getCharacteristic(BLEUUID((uint16_t)0xFF02));

		if (remoteCommand == nullptr)
		{
			Serial.println("Failed to find our characteristic command");
			return false;
		}

		if (remoteNotify == nullptr)
		{
			Serial.println("Failed to find our characteristic notify");
			return false;
		}
		Serial.println("Camera BLE service and characteristic found");

		remoteCommand->registerForNotify(commandNotifyCallback);
		remoteNotify->registerForNotify(notifyNotifyCallback);

		connected = true;

		return true;
	}
	else
	{
		Serial.println(" - fail to BLE connect");
		return false;
	}
}

// Accept any pair request from Camera
class MySecurityCallbacks : public BLESecurityCallbacks
{

	uint32_t onPassKeyRequest()
	{
		Serial.println("PassKeyRequest");
		return 123456;
	}

	void onPassKeyNotify(uint32_t pass_key)
	{
		Serial.print("The passkey Notify number:");
		Serial.println(pass_key);
	}

	bool onSecurityRequest()
	{
		Serial.println("SecurityRequest");
		return true;
	}

	void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
	{
		Serial.println("Authentication Complete");
		if (cmpl.success)
		{
			Serial.println("Pairing success");
		}
		else
		{
			Serial.println("Pairing failed");
		}
	}

	bool onConfirmPIN(uint32_t pin)
	{
		return true;
	}
};

void pairOrConnect()
{
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true);
	Serial.println("BLE: Looking for camera");
	pBLEScan->start(30);
	Serial.println("BLE: end of searching");
}

void setup()
{
	Serial.begin(115200);						  // default boot baudrate
	esp_log_level_set("*", ESP_LOG_VERBOSE); // verbose logs
	pinMode(SHOOT_BUTTON, INPUT_PULLUP);
	// pinMode(LED_ONBOARD, OUTPUT);

	// Init BLE device
	BLEDevice::init("Blt RC 0.0");
	BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
	BLEDevice::setSecurityCallbacks(new MySecurityCallbacks());

	// Retrieve a Scanner and set the callback we want to use to be informed when we
	// have detected a new device.  Specify that we want active scanning and start the
	// scan to run for 30 seconds.
	pairOrConnect();
	take_serial = 1;
	// take_shoot = digitalRead(SHOOT_BUTTON);
	// digitalWrite(LED_ONBOARD, LOW);
	// delay(1000);
	// Serial.println("Setup done");
}

void loop()
{

	if (doPairing || doConnect)
	{
		connectToServer(*pServerAddress);
	}

	if (connected)
	{
		if (digitalRead(SHOOT_BUTTON) == LOW)
		{
			take_shoot = 1;
			delay(100);
		}
		if (digitalRead(SHOOT_BUTTON) == HIGH)
		{
			take_shoot = 0;
			delay(100);
		}

		if (take_shoot == 1)
		{
			first_press = 1;
			if (was_pressed == 0)
			{
				remoteCommand->writeValue(Shutter_Half_Down, 2, true);
				currentMillis = millis();
				previousMillis = currentMillis;
				was_pressed = 1;
				was_released = 0;
				// delay(100);
			}
			if (was_pressed == 1)
			{
				if (millis() - previousMillis >= time_full_dwn) // все еще держу...
				{
					if (fully_down_send == 0)
					{
						remoteCommand->writeValue(Shutter_Fully_Down, 2, true);
						fully_down_send = 1;
					}
				}
			}
		}
		if (take_shoot == 0 and first_press == 1)
		{
			if (was_released == 0)
			{
				remoteCommand->writeValue(Shutter_Half_Up, 2, true);
				currentMillis = millis();
				previousMillis = currentMillis;
				was_released = 1;
			}
			if (was_released == 1)
			{
				if (millis() - previousMillis >= time_full_up) // Логика, что прошло время, уже не держу...
				{
					if (fully_up_send == 0)
					{
						remoteCommand->writeValue(Shutter_Fully_Up, 2, true);
						fully_up_send = 1;
						was_pressed = 0;
					}
				}
			}
		}
	}
	else
	{
		pairOrConnect();
		delay(500);
	}
}