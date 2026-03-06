#include <Arduino.h>
// #include <esp_wifi.h>
#include <BLEDevice.h>

// Please enter your camera bluetooth name. My Sony a6400 name is:
#define bleServerName "ILCE-6400"
#define SHOOT_BUTTON 1
#define FOCUS_BUTTON 3
#define BLUE_LIGTH 7
#define GREEN_LIGTH 6

int8_t take_focus = 0;
int8_t take_shoot = 0;
int8_t fully_up_send = 0;
int8_t half_down_send = 0;
int8_t full_down_send = 0;
boolean led_status = LOW;
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
unsigned long previousConnTime = 0;
unsigned long currentMillis = millis();
const long time_full_dwn = 700;
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
		{																	// Check if the name of the advertiser matches
			advertisedDevice.getScan()->stop();								// Scan can be stopped, we found what we are looking for
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

	uint32_t onPassKeyRequest() // Вызывается, когда камера запрашивает PIN-код
	{
		Serial.println("PassKeyRequest");
		return 123456; // Возвразает статический код для соединения
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

	void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) // Вызывается при завершении процесса сопряжения
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
	BLEScan *pBLEScan = BLEDevice::getScan();								   // ESP32 начинает «слушать» эфир на наличие рекламных пакетов (Advertising) от других устройств
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks()); // Как только сканер находит любое BLE-устройство, он вызывает функцию MyAdvertisedDeviceCallbacks, «Если имя устройства "Camera", то запомнить его адрес».
	pBLEScan->setActiveScan(true);											   // ESP32 отправляет запрос (Scan Request) найденному устройству, чтобы получить дополнительные данные (например, полное имя или список сервисов)
	// pBLEScan->setInterval(100); // время, через которое сканер переключается на другой радиоканал (Bluetooth использует 3 рекламных канала)
	// pBLEScan->setWindow(1344); // время, в течение которого сканер активно слушает эфир внутри каждого интервала.
	Serial.println("BLE: Looking for camera");
	pBLEScan->start(30); // Запускает процесс на 30 секунд. Это блокирующая функция — выполнение кода остановится на этой строке, пока время не выйдет или пока вы не вызовете stop() вручную внутри колбэка.
	Serial.println("BLE: end of searching");
}

void setup()
{
	/*
	// 1. Создаем дефолтный конфиг (занимает минимум RAM в стеке)
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	// 2. Инициализируем драйвер, чтобы получить контроль над радиомодулем
	if (esp_wifi_init(&cfg) == ESP_OK)
	{
		// 3. Явно останавливаем работу (отключает питание RF-блока)
		esp_wifi_stop();
		// 4. Выгружаем драйвер из памяти
		esp_wifi_deinit();
	}
	*/
	setCpuFrequencyMhz(80);					 // Установить частоту 80 МГц (вместо стандартных 160)
	Serial.begin(9600);						 // default boot baudrate
	esp_log_level_set("*", ESP_LOG_VERBOSE); // verbose logs
	pinMode(SHOOT_BUTTON, INPUT);
	pinMode(FOCUS_BUTTON, INPUT);
	pinMode(BLUE_LIGTH, OUTPUT);
	pinMode(GREEN_LIGTH, OUTPUT);

	// Init BLE device
	BLEDevice::init("Blt RC 0.0");						// Инициализация BLE с названием "Blt RC 0.0"
	BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT); // устанавливает уровень шифрования
	// Установка мощности BLE для esp32-C3 по типам пакетов
	// min: ESP_PWR_LVL_N12 (-12 dBm), max: ESP_PWR_LVL_P18 (+18 dBm), default: ESP_PWR_LVL_P3 (+3 dBm)
	BLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_DEFAULT);
	BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);	// Для видимости при поиске
	BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN); // Для режима сканирования
	BLEDevice::setSecurityCallbacks(new MySecurityCallbacks()); // команда для авторизации на стороне esp32
	// Retrieve a Scanner and set the callback we want to use to be informed when we
	// have detected a new device.  Specify that we want active scanning and start the
	// scan to run for 30 seconds.
	pairOrConnect();
	previousConnTime = millis();
}

void loop()
{
	currentMillis = millis();
	if (doPairing || doConnect)
	{
		connectToServer(*pServerAddress);
	}
	// Чтение кнопки ФОКУС
	if (digitalRead(FOCUS_BUTTON) == LOW)
	{
		take_focus = 1;
	}
	if (digitalRead(FOCUS_BUTTON) == HIGH)
	{
		take_focus = 0;
	}
	// Чтение кнопки СЪЕМКА
	if (digitalRead(SHOOT_BUTTON) == LOW)
	{
		take_shoot = 1;
		digitalWrite(BLUE_LIGTH, LOW);
		delay(5);
		digitalWrite(GREEN_LIGTH, HIGH); // поджиг зеленого LED при съемке
	}
	if (digitalRead(SHOOT_BUTTON) == HIGH)
	{
		take_shoot = 0;
		digitalWrite(GREEN_LIGTH, LOW);
		delay(5);
	}

	// Если установлена связь с камерой
	if (connected)
	{
		// Включение синего светодиода, если не нажата кнопка СЪЕМКА
		if (take_shoot == 0 and digitalRead(BLUE_LIGTH) == LOW)
		{
			digitalWrite(BLUE_LIGTH, HIGH);
		}
		// Логика отправки кодов в камеру, в зависимости от активации кнопок Фокус и Съемка
		if (take_shoot == 0 and take_focus == 1) // Если нажата только кнопка ФОКУС
		{
			if (full_down_send == 1)
			{
				remoteCommand->writeValue(Shutter_Half_Up, 2, true); // Нужно отправлять все эти шаги,
				delay(10);
				remoteCommand->writeValue(Shutter_Fully_Up, 2, true); // иначе в камера корректно не отработает
				delay(10);
			}
			if (half_down_send == 0)
			{
				remoteCommand->writeValue(Shutter_Half_Down, 2, true);
				half_down_send = 1;
				fully_up_send = 0;
				full_down_send = 0;
				delay(10);
			}
		}
		if (take_focus == 1 and take_shoot == 1) // Нажаты обе кнопки, ФОКУС и СЪЕМКА
		{
			if (full_down_send == 0)
			{
				remoteCommand->writeValue(Shutter_Fully_Down, 2, true);
				full_down_send = 1;
				half_down_send = 0;
				delay(10);
			}
		}
		if (take_shoot == 0 and take_focus == 0) // Обе кнопки отпущены
		{
			if (fully_up_send == 0)
			{
				remoteCommand->writeValue(Shutter_Half_Up, 2, true); // Нужно отправлять все эти шаги,
				delay(10);
				remoteCommand->writeValue(Shutter_Fully_Up, 2, true); // иначе в камера корректно не отработает
				fully_up_send = 1;
				full_down_send = 0;
				half_down_send = 0;
				delay(10);
			}
		}
	}
	else // Если нет связи с камерой
	{
		if (currentMillis - previousMillis >= 1000)
		{
			led_status = !led_status;
			digitalWrite(BLUE_LIGTH, led_status); // Хотел поморгать синим LED, не не моргает...
			previousMillis = millis();
		}
		if (currentMillis - previousConnTime >= 2000)
		{
			pairOrConnect(); // Вызов функции соединения с камерой
			previousConnTime = millis();
		}
	}
}
