// Термопринтер Nippon NP-F3092 + ESP32-S3 + Telegram Bot
// RS232 через MAX3232, кириллица CP1251, WiFi + Telegram

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========== НАСТРОЙКИ — ИЗМЕНИ ПОД СЕБЯ ==========

const char* WIFI_SSID     = "ТВОЙ_WIFI";        // Имя WiFi сети
const char* WIFI_PASSWORD = "ТВОЙ_ПАРОЛЬ";       // Пароль WiFi
const char* BOT_TOKEN     = "ТВОЙ_ТОКЕН_БОТА";   // Токен от @BotFather

// =================================================

#define RXD2 17  // Пин RX для Serial2 (подключен к TX MAX3232)
#define TXD2 18  // Пин TX для Serial2 (подключен к RX MAX3232)

// Telegram
long lastUpdateId = 0;                // ID последнего обработанного сообщения
unsigned long lastPollTime = 0;       // Время последнего опроса
const unsigned long POLL_INTERVAL = 2000;  // Интервал опроса (2 сек)

void setup() {
    // Инициализация Serial для отладки
    Serial.begin(115200);
    delay(1000);
    Serial.println("Инициализация принтера...");

    // Инициализация Serial2 для принтера
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    Serial2.setRxBufferSize(1024);
    Serial2.setTxBufferSize(1024);

    delay(500);

    // Настройка кириллицы
    setupCyrillic();

    // Подключение к WiFi
    connectWiFi();

    Serial.println("Принтер готов!");
    Serial.println("Команды Serial Monitor:");
    Serial.println("  t - полный тест");
    Serial.println("  s - короткий тест");
    Serial.println("  a - расширенный тест (чек на русском)");
    Serial.println("  r - тест кириллицы");
    Serial.println("  b - тест штрихкода и QR");
    Serial.println("  f - рамка из * с текстом ТЕСТОВОЕ");
    Serial.println("Telegram бот активен — отправь сообщение боту!");
}

void loop() {
    // Команды через Serial Monitor (как раньше)
    if (Serial.available()) {
        char cmd = Serial.read();

        switch(cmd) {
            case 't': testPrint(); break;
            case 's': shortTest(); break;
            case 'a': advancedTest(); break;
            case 'r': cyrillicTest(); break;
            case 'b': barcodeQRTest(); break;
            case 'f': frameTest(); break;
            default: Serial.println("Неизвестная команда");
        }
    }

    // Опрос Telegram бота
    if (millis() - lastPollTime >= POLL_INTERVAL) {
        lastPollTime = millis();

        // Переподключение WiFi если отвалился
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi потерян, переподключаюсь...");
            connectWiFi();
        }

        if (WiFi.status() == WL_CONNECTED) {
            checkTelegram();
        }
    }
}

// ========== WiFi ==========

void connectWiFi() {
    Serial.print("Подключение к WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi подключен! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("WiFi не подключен. Работаю без Telegram.");
    }
}

// ========== TELEGRAM BOT ==========

void checkTelegram() {
    HTTPClient http;

    String url = "https://api.telegram.org/bot";
    url += BOT_TOKEN;
    url += "/getUpdates?limit=5&timeout=0";
    if (lastUpdateId > 0) {
        url += "&offset=";
        url += String(lastUpdateId + 1);
    }

    http.begin(url);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        parseMessages(payload);
    } else if (httpCode > 0) {
        Serial.print("Telegram HTTP ошибка: ");
        Serial.println(httpCode);
    } else {
        Serial.print("Telegram ошибка соединения: ");
        Serial.println(http.errorToString(httpCode));
    }

    http.end();
}

void parseMessages(String payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("JSON ошибка: ");
        Serial.println(error.c_str());
        return;
    }

    if (!doc["ok"].as<bool>()) return;

    JsonArray results = doc["result"].as<JsonArray>();

    for (JsonObject update : results) {
        long updateId = update["update_id"].as<long>();
        lastUpdateId = updateId;

        // Получаем текст сообщения
        const char* text = update["message"]["text"];
        if (!text) continue;

        // Получаем имя отправителя
        const char* firstName = update["message"]["from"]["first_name"];
        long chatId = update["message"]["chat"]["id"];

        Serial.print("Telegram от ");
        Serial.print(firstName ? firstName : "???");
        Serial.print(": ");
        Serial.println(text);

        // Обработка команд бота
        String msg = String(text);

        if (msg == "/start") {
            sendTelegramMessage(chatId, "Привет! Я бот-принтер.\nОтправь мне текст — я его распечатаю.\n\nКоманды:\n/status - статус принтера\n/cut - отрезать бумагу\n/test - тестовая печать");
        }
        else if (msg == "/status") {
            String status = "Принтер онлайн\nWiFi: ";
            status += WiFi.SSID();
            status += "\nIP: ";
            status += WiFi.localIP().toString();
            status += "\nСигнал: ";
            status += String(WiFi.RSSI());
            status += " dBm";
            sendTelegramMessage(chatId, status);
        }
        else if (msg == "/cut") {
            feedDots(80);
            partialCut();
            sendTelegramMessage(chatId, "Бумага отрезана!");
        }
        else if (msg == "/test") {
            shortTest();
            sendTelegramMessage(chatId, "Тестовая печать выполнена!");
        }
        else {
            // Печатаем текст сообщения
            printTelegramMessage(firstName, msg);
            sendTelegramMessage(chatId, "Напечатано!");
        }
    }
}

void printTelegramMessage(const char* from, String text) {
    setupCyrillic();

    // Заголовок
    setAlignment(1);
    printCyrillicLine("--- TELEGRAM ---");
    setAlignment(0);

    // Отправитель
    if (from) {
        String fromLine = "От: ";
        fromLine += from;
        printCyrillicLine(fromLine);
    }
    printCyrillicLine("------------------------------------------------");

    // Текст сообщения — разбиваем на строки по 48 символов
    printWrappedText(text);

    printCyrillicLine("------------------------------------------------");

    // Подача и отрез
    feedDots(80);
    partialCut();
}

// Печать текста с переносом строк (48 символов на строку для Font A)
void printWrappedText(String text) {
    int charCount = 0;
    String line = "";
    int i = 0;

    while (i < (int)text.length()) {
        uint8_t c = text[i];

        // Перенос строки в тексте
        if (c == '\n') {
            printCyrillicLine(line);
            line = "";
            charCount = 0;
            i++;
            continue;
        }

        // Определяем длину UTF-8 символа
        int charBytes = 1;
        if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;

        // Если строка полная — печатаем и начинаем новую
        if (charCount >= 48) {
            printCyrillicLine(line);
            line = "";
            charCount = 0;
        }

        // Добавляем символ (все байты)
        for (int j = 0; j < charBytes && (i + j) < (int)text.length(); j++) {
            line += (char)text[i + j];
        }
        charCount++;
        i += charBytes;
    }

    // Печатаем остаток
    if (line.length() > 0) {
        printCyrillicLine(line);
    }
}

void sendTelegramMessage(long chatId, String text) {
    HTTPClient http;

    String url = "https://api.telegram.org/bot";
    url += BOT_TOKEN;
    url += "/sendMessage";

    // Экранируем JSON
    String jsonBody = "{\"chat_id\":";
    jsonBody += String(chatId);
    jsonBody += ",\"text\":\"";
    // Простое экранирование кавычек и переносов
    for (int i = 0; i < (int)text.length(); i++) {
        char c = text[i];
        if (c == '"') jsonBody += "\\\"";
        else if (c == '\n') jsonBody += "\\n";
        else if (c == '\\') jsonBody += "\\\\";
        else jsonBody += c;
    }
    jsonBody += "\"}";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int httpCode = http.POST(jsonBody);

    if (httpCode != 200) {
        Serial.print("Telegram sendMessage ошибка: ");
        Serial.println(httpCode);
    }

    http.end();
}

// ========== НАСТРОЙКА КИРИЛЛИЦЫ ==========

void setupCyrillic() {
    printerInit();  // ESC @
    delay(200);
    selectCodepage(0x04);  // CP1251
}

// ========== ТАБЛИЦА ПЕРЕКОДИРОВКИ UTF-8 -> CP1251 ==========

uint8_t utf8ToCp1251(uint16_t utf8Code) {
    if (utf8Code >= 0x0410 && utf8Code <= 0x042F) {
        return 0xC0 + (utf8Code - 0x0410);  // А-Я
    }
    else if (utf8Code >= 0x0430 && utf8Code <= 0x044F) {
        return 0xE0 + (utf8Code - 0x0430);  // а-я
    }
    else if (utf8Code == 0x0401) return 0xA8;  // Ё
    else if (utf8Code == 0x0451) return 0xB8;  // ё

    return utf8Code & 0xFF;
}

// Печать строки с поддержкой кириллицы
void printCyrillicLine(String text) {
    int i = 0;
    while (i < (int)text.length()) {
        uint8_t c = text[i];

        if ((c & 0x80) == 0) {
            Serial2.write(c);
            i++;
        }
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < (int)text.length()) {
                uint8_t c2 = text[i + 1];
                uint16_t utf8Code = ((c & 0x1F) << 6) | (c2 & 0x3F);
                Serial2.write(utf8ToCp1251(utf8Code));
                i += 2;
            } else i++;
        }
        else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < (int)text.length()) {
                uint8_t c2 = text[i + 1];
                uint8_t c3 = text[i + 2];
                uint16_t utf8Code = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                Serial2.write(utf8ToCp1251(utf8Code));
                i += 3;
            } else i++;
        }
        else i++;
    }

    Serial2.write(0x0A);
    Serial2.flush();
    delay(100);
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

void sendCommand(uint8_t cmd1, uint8_t cmd2) {
    Serial2.write(cmd1);
    Serial2.write(cmd2);
    Serial2.flush();
    delay(50);
}

void sendCommand(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3) {
    Serial2.write(cmd1);
    Serial2.write(cmd2);
    Serial2.write(cmd3);
    Serial2.flush();
    delay(50);
}

void printLine(String text) {
    Serial2.print(text);
    Serial2.write(0x0A);
    Serial2.flush();
    delay(100);
}

// ========== БАЗОВЫЕ ESC/POS-КОМАНДЫ ==========

void printerInit() {
    sendCommand(0x1B, 0x40);  // ESC @
}

void selectCodepage(uint8_t codepage) {
    sendCommand(0x1B, 0x74, codepage);  // ESC t n
}

void feedDots(uint8_t dots) {
    sendCommand(0x1B, 0x4A, dots);  // ESC J n
    Serial2.flush();
    delay(300);
}

void partialCut() {
    sendCommand(0x1B, 0x6D);  // ESC m
    Serial2.flush();
    delay(600);
}

void setAlignment(uint8_t align) {
    sendCommand(0x1B, 0x61, align);
}

void setBold(bool enable) {
    sendCommand(0x1B, 0x45, enable ? 1 : 0);
}

// ========== ТЕСТОВЫЕ ФУНКЦИИ ==========

void cyrillicTest() {
    Serial.println("Тест кириллицы...");
    setupCyrillic();

    setAlignment(1);
    setBold(true);
    printCyrillicLine("ТЕСТ КИРИЛЛИЦЫ");
    setBold(false);

    setAlignment(0);
    printCyrillicLine("--------------------------------");
    printCyrillicLine("Русский алфавит:");
    printCyrillicLine("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ");
    printCyrillicLine("абвгдеёжзийклмнопрстуфхцчшщъыьэюя");
    printCyrillicLine("--------------------------------");
    printCyrillicLine("Цифры: 0123456789");
    printCyrillicLine("Знаки: !@#$%^&*()_+-=[]{}");
    printCyrillicLine("--------------------------------");

    setAlignment(1);
    printCyrillicLine("Привет, мир!");
    printCyrillicLine("Hello, World!");

    feedDots(100);
    partialCut();
    Serial.println("Тест кириллицы завершен!");
}

void testPrint() {
    Serial.println("Запуск тестовой печати...");
    printerInit();
    delay(200);

    setAlignment(1);
    setBold(true);
    printLine("=== PRINTER TEST ===");
    setBold(false);

    setAlignment(0);
    printLine("Nippon NP-F309");
    printLine("ESP32-S3 + RS232");
    printLine("Date: " + String(__DATE__));
    printLine("Time: " + String(__TIME__));

    setAlignment(1);
    printLine("====================");

    feedDots(80);
    partialCut();
    Serial.println("Печать завершена!");
}

void shortTest() {
    Serial.println("Короткий тест...");
    printerInit();
    delay(200);
    printLine("Quick test OK");
    feedDots(60);
    partialCut();
    Serial.println("Готово!");
}

void advancedTest() {
    Serial.println("Расширенный тест (чек на русском)...");
    setupCyrillic();

    setAlignment(1);
    setBold(true);
    printCyrillicLine("МАГАЗИН ЭЛЕКТРОНИКА");
    setBold(false);
    printCyrillicLine("ул. Примерная, д. 123");
    printCyrillicLine("Тел: +7 (123) 456-78-90");
    printCyrillicLine("------------------------");

    setAlignment(0);
    printCyrillicLine("Товар             Цена");
    printCyrillicLine("------------------------");
    printCyrillicLine("Arduino UNO      1500 р");
    printCyrillicLine("ESP32-S3          890 р");
    printCyrillicLine("Резистор 10к        5 р");
    printCyrillicLine("Макетка           250 р");
    printCyrillicLine("------------------------");

    setBold(true);
    printCyrillicLine("ИТОГО:           2645 р");
    setBold(false);

    printCyrillicLine("------------------------");
    setAlignment(1);
    printCyrillicLine("Спасибо за покупку!");
    printLine(String(__DATE__) + " " + String(__TIME__));

    feedDots(100);
    partialCut();
    Serial.println("Чек распечатан!");
}

// ========== ШТРИХКОД И QR ==========

void printBarcode(String data) {
    Serial2.write(0x1D); Serial2.write(0x68); Serial2.write(100);
    Serial2.flush(); delay(50);
    Serial2.write(0x1D); Serial2.write(0x77); Serial2.write(3);
    Serial2.flush(); delay(50);
    Serial2.write(0x1D); Serial2.write(0x48); Serial2.write(2);
    Serial2.flush(); delay(50);

    Serial2.write(0x1D); Serial2.write(0x6B); Serial2.write(0x04);
    Serial2.print("*" + data + "*");
    Serial2.write(0x00);
    Serial2.flush();
    delay(500);
}

void printQRCode(String data) {
    Serial2.write(0x1B); Serial2.write(0x71);
    Serial2.write(4); Serial2.write(0); Serial2.write(0); Serial2.write(0);
    uint16_t len = data.length();
    Serial2.write(len & 0xFF);
    Serial2.write((len >> 8) & 0xFF);
    Serial2.print(data);
    Serial2.flush();
    delay(1000);
}

void barcodeQRTest() {
    Serial.println("Тест штрихкода и QR...");
    setupCyrillic();

    setAlignment(1);
    printCyrillicLine("Штрихкод CODE39:");
    setAlignment(0);
    printBarcode("123456789");

    setAlignment(1);
    printCyrillicLine("");
    printCyrillicLine("QR код:");
    printQRCode("https://github.com");

    feedDots(100);
    partialCut();
    Serial.println("Готово!");
}

// ========== РАМКА ==========

void frameTest() {
    Serial.println("Печать рамки из *...");
    setupCyrillic();

    const uint8_t lineWidth = 48;
    String topBottom = "";
    for (uint8_t i = 0; i < lineWidth; i++) topBottom += "*";

    String middle = "*";
    for (uint8_t i = 0; i < lineWidth - 2; i++) middle += " ";
    middle += "*";

    printCyrillicLine(topBottom);
    for (uint8_t i = 0; i < 3; i++) printCyrillicLine(middle);

    // Текст: двойная ширина + двойная высота + жирный, по центру без *
    setAlignment(1);
    sendCommand(0x1B, 0x21, 0x38);
    printCyrillicLine("ТЕСТОВОЕ");
    sendCommand(0x1B, 0x21, 0x00);
    setAlignment(0);

    for (uint8_t i = 0; i < 3; i++) printCyrillicLine(middle);
    printCyrillicLine(topBottom);

    feedDots(100);
    partialCut();
    Serial.println("Рамка распечатана!");
}
