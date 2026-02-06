// Термопринтер Nippon NP-F3092 + ESP32-S3 + Telegram Bot
// RS232 через MAX3232, кириллица CP1251, WiFi + Telegram
// Поддержка печати текста и изображений (JPEG → ч/б растр)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>

// ========== НАСТРОЙКИ — ИЗМЕНИ ПОД СЕБЯ ==========

const char* WIFI_SSID     = "ТВОЙ_WIFI";        // Имя WiFi сети
const char* WIFI_PASSWORD = "ТВОЙ_ПАРОЛЬ";       // Пароль WiFi
const char* BOT_TOKEN     = "ТВОЙ_ТОКЕН_БОТА";   // Токен от @BotFather

// =================================================

#define RXD2 17
#define TXD2 18

// Telegram
long lastUpdateId = 0;
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;

// Глобальный TLS-клиент (экономим память, не создаём каждый раз)
WiFiClientSecure secureClient;

// Изображения
#define IMG_WIDTH       576
#define IMG_BYTES_ROW   72    // 576 / 8
#define IMG_MAX_HEIGHT  1200
#define MAX_JPEG_SIZE   150000

JPEGDEC jpeg;
uint8_t* imgBitmap = nullptr;
int imgDecodedWidth = 0;
int imgHeight = 0;
int imgOffsetX = 0;

// Bayer 4×4 матрица дизеринга (упорядоченный дизеринг)
const uint8_t bayer4x4[4][4] = {
    {  0, 128,  32, 160},
    {192,  64, 224,  96},
    { 48, 176,  16, 144},
    {240, 112, 208,  80}
};

// ========== SETUP / LOOP ==========

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Инициализация принтера...");

    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    Serial2.setRxBufferSize(1024);
    Serial2.setTxBufferSize(1024);
    delay(500);

    setupCyrillic();

    // TLS без проверки сертификата (для Telegram API)
    secureClient.setInsecure();

    connectWiFi();

    Serial.println("Принтер готов!");
    Serial.println("Команды Serial Monitor:");
    Serial.println("  t - полный тест    s - короткий тест");
    Serial.println("  a - чек на русском r - тест кириллицы");
    Serial.println("  b - штрихкод/QR    f - рамка");
    Serial.println("Telegram: текст → печать, фото → печать картинки");
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        switch(cmd) {
            case 't': testPrint(); break;
            case 's': shortTest(); break;
            case 'a': advancedTest(); break;
            case 'r': cyrillicTest(); break;
            case 'b': barcodeQRTest(); break;
            case 'f': frameTest(); break;
        }
    }

    if (millis() - lastPollTime >= POLL_INTERVAL) {
        lastPollTime = millis();
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
        Serial.print("\nWiFi подключен! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi не подключен.");
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

    http.begin(secureClient, url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code == 200) {
        parseMessages(http.getString());
    }
    http.end();
}

void parseMessages(String payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return;
    if (!doc["ok"].as<bool>()) return;

    for (JsonObject update : doc["result"].as<JsonArray>()) {
        lastUpdateId = update["update_id"].as<long>();
        JsonObject message = update["message"];
        if (message.isNull()) continue;

        long chatId = message["chat"]["id"].as<long>();
        const char* firstName = message["from"]["first_name"];

        // Фото?
        if (!message["photo"].isNull()) {
            Serial.println("Получено фото из Telegram");
            handlePhoto(message, chatId);
            continue;
        }

        // Текст?
        const char* text = message["text"];
        if (!text) continue;

        Serial.printf("Telegram от %s: %s\n", firstName ? firstName : "???", text);
        String msg = String(text);

        if (msg == "/start") {
            sendTelegramMessage(chatId,
                "Привет! Я бот-принтер.\n"
                "Отправь текст — распечатаю.\n"
                "Отправь фото — тоже распечатаю!\n\n"
                "Команды:\n"
                "/status - статус принтера\n"
                "/cut - отрезать бумагу\n"
                "/test - тестовая печать");
        }
        else if (msg == "/status") {
            String s = "Принтер онлайн\nWiFi: " + WiFi.SSID()
                     + "\nIP: " + WiFi.localIP().toString()
                     + "\nСигнал: " + String(WiFi.RSSI()) + " dBm"
                     + "\nСвободно RAM: " + String(ESP.getFreeHeap() / 1024) + " KB";
            sendTelegramMessage(chatId, s);
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
            printTelegramMessage(firstName, msg);
            sendTelegramMessage(chatId, "Напечатано!");
        }
    }
}

void printTelegramMessage(const char* from, String text) {
    setupCyrillic();
    setAlignment(1);
    printCyrillicLine("--- TELEGRAM ---");
    setAlignment(0);

    if (from) {
        String fromLine = "От: ";
        fromLine += from;
        printCyrillicLine(fromLine);
    }
    printCyrillicLine("------------------------------------------------");
    printWrappedText(text);
    printCyrillicLine("------------------------------------------------");

    feedDots(80);
    partialCut();
}

void printWrappedText(String text) {
    int charCount = 0;
    String line = "";
    int i = 0;

    while (i < (int)text.length()) {
        uint8_t c = text[i];
        if (c == '\n') {
            printCyrillicLine(line);
            line = "";
            charCount = 0;
            i++;
            continue;
        }

        int charBytes = 1;
        if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;

        if (charCount >= 48) {
            printCyrillicLine(line);
            line = "";
            charCount = 0;
        }

        for (int j = 0; j < charBytes && (i + j) < (int)text.length(); j++) {
            line += (char)text[i + j];
        }
        charCount++;
        i += charBytes;
    }
    if (line.length() > 0) printCyrillicLine(line);
}

void sendTelegramMessage(long chatId, String text) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot";
    url += BOT_TOKEN;
    url += "/sendMessage";

    String body = "{\"chat_id\":";
    body += String(chatId);
    body += ",\"text\":\"";
    for (int i = 0; i < (int)text.length(); i++) {
        char c = text[i];
        if (c == '"') body += "\\\"";
        else if (c == '\n') body += "\\n";
        else if (c == '\\') body += "\\\\";
        else body += c;
    }
    body += "\"}";

    http.begin(secureClient, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    http.POST(body);
    http.end();
}

// ========== ПЕЧАТЬ ИЗОБРАЖЕНИЙ ==========

// Callback для JPEGDEC — вызывается для каждого блока MCU
int jpegDrawCallback(JPEGDRAW *pDraw) {
    for (int j = 0; j < pDraw->iHeight; j++) {
        for (int i = 0; i < pDraw->iWidth; i++) {
            int srcX = pDraw->x + i;
            int srcY = pDraw->y + j;
            if (srcX >= imgDecodedWidth || srcY >= imgHeight) continue;

            uint16_t pixel = pDraw->pPixels[j * pDraw->iWidth + i];

            // RGB565 → оттенки серого
            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            uint8_t b = (pixel & 0x1F) << 3;
            uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);

            // Упорядоченный дизеринг Bayer 4×4
            uint8_t threshold = bayer4x4[srcY & 3][srcX & 3];

            if (gray < threshold) {
                // Чёрная точка
                int bx = srcX + imgOffsetX;
                int byteIdx = srcY * IMG_BYTES_ROW + bx / 8;
                imgBitmap[byteIdx] |= (1 << (7 - (bx & 7)));
            }
        }
    }
    return 1;
}

void handlePhoto(JsonObject message, long chatId) {
    JsonArray photos = message["photo"].as<JsonArray>();
    if (photos.size() == 0) return;

    // Выбираем фото ближайшее к 576px по ширине (но не меньше)
    const char* bestId = nullptr;
    int bestW = 0;

    for (JsonObject p : photos) {
        int w = p["width"].as<int>();
        if (w >= IMG_WIDTH && (bestW == 0 || w < bestW)) {
            bestW = w;
            bestId = p["file_id"];
        }
    }
    // Если нет >= 576, берём самое большое
    if (!bestId) {
        for (JsonObject p : photos) {
            int w = p["width"].as<int>();
            if (w > bestW) {
                bestW = w;
                bestId = p["file_id"];
            }
        }
    }

    if (!bestId) {
        sendTelegramMessage(chatId, "Ошибка: не удалось получить фото");
        return;
    }

    // Получаем путь к файлу через Telegram API
    String filePath = getTelegramFilePath(bestId);
    if (filePath.length() == 0) {
        sendTelegramMessage(chatId, "Ошибка: getFile не удался");
        return;
    }

    String fileUrl = "https://api.telegram.org/file/bot";
    fileUrl += BOT_TOKEN;
    fileUrl += "/";
    fileUrl += filePath;

    Serial.print("Скачиваю: ");
    Serial.println(fileUrl);
    sendTelegramMessage(chatId, "Скачиваю и печатаю фото...");

    // Скачиваем JPEG
    size_t jpegSize = 0;
    uint8_t* jpegData = downloadFile(fileUrl, &jpegSize);

    if (!jpegData) {
        sendTelegramMessage(chatId, "Ошибка: не удалось скачать фото");
        return;
    }

    Serial.printf("JPEG: %d байт\n", jpegSize);

    // Декодируем и печатаем
    bool ok = decodeAndPrintJpeg(jpegData, jpegSize);
    free(jpegData);

    sendTelegramMessage(chatId, ok ? "Фото напечатано!" : "Ошибка печати фото");
}

String getTelegramFilePath(const char* fileId) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot";
    url += BOT_TOKEN;
    url += "/getFile?file_id=";
    url += fileId;

    http.begin(secureClient, url);
    http.setTimeout(10000);
    int code = http.GET();

    String path = "";
    if (code == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            if (doc["ok"].as<bool>()) {
                path = doc["result"]["file_path"].as<String>();
            }
        }
    }
    http.end();
    return path;
}

uint8_t* downloadFile(String url, size_t* outSize) {
    HTTPClient http;
    http.begin(secureClient, url);
    http.setTimeout(15000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("Download HTTP error: %d\n", code);
        http.end();
        return nullptr;
    }

    int len = http.getSize();
    if (len <= 0 || len > MAX_JPEG_SIZE) {
        Serial.printf("Bad file size: %d\n", len);
        http.end();
        return nullptr;
    }

    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        Serial.println("malloc failed (JPEG)");
        http.end();
        return nullptr;
    }

    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = 0;
    unsigned long deadline = millis() + 15000;

    while (bytesRead < len && millis() < deadline) {
        if (stream->available()) {
            int n = stream->readBytes(buf + bytesRead,
                                      min((int)stream->available(), len - bytesRead));
            bytesRead += n;
        }
        delay(1);
    }
    http.end();

    if (bytesRead != len) {
        Serial.printf("Incomplete: %d / %d\n", bytesRead, len);
        free(buf);
        return nullptr;
    }

    *outSize = bytesRead;
    return buf;
}

bool decodeAndPrintJpeg(uint8_t* jpegData, size_t jpegSize) {
    if (!jpeg.openRAM(jpegData, jpegSize, jpegDrawCallback)) {
        Serial.println("JPEG open failed");
        return false;
    }

    int origW = jpeg.getWidth();
    int origH = jpeg.getHeight();
    Serial.printf("JPEG: %dx%d\n", origW, origH);

    // Масштаб: выбираем ближайший к 576px
    int scale = 0;
    if (origW > 4608)      scale = JPEG_SCALE_EIGHTH;   // /8
    else if (origW > 2304) scale = JPEG_SCALE_QUARTER;  // /4
    else if (origW > 1152) scale = JPEG_SCALE_HALF;     // /2

    int divisor = scale ? scale : 1;
    imgDecodedWidth = origW / divisor;
    imgHeight = origH / divisor;

    if (imgDecodedWidth > IMG_WIDTH) imgDecodedWidth = IMG_WIDTH;
    if (imgHeight > IMG_MAX_HEIGHT) imgHeight = IMG_MAX_HEIGHT;

    // Центрируем если уже 576
    imgOffsetX = (IMG_WIDTH - imgDecodedWidth) / 2;
    if (imgOffsetX < 0) imgOffsetX = 0;

    // Выделяем bitmap (calloc = заполнен нулями = белый)
    size_t bitmapSize = (size_t)IMG_BYTES_ROW * imgHeight;
    imgBitmap = (uint8_t*)calloc(1, bitmapSize);
    if (!imgBitmap) {
        Serial.printf("malloc failed (bitmap %d bytes)\n", bitmapSize);
        jpeg.close();
        return false;
    }

    Serial.printf("Decode: scale=1/%d, size=%dx%d, offset=%d\n",
                  divisor, imgDecodedWidth, imgHeight, imgOffsetX);

    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg.decode(0, 0, scale);
    jpeg.close();

    // Печатаем
    setupCyrillic();
    printRasterImage(imgBitmap, imgHeight);
    feedDots(80);
    partialCut();

    free(imgBitmap);
    imgBitmap = nullptr;

    Serial.println("Изображение напечатано!");
    return true;
}

// Печать растрового изображения через ESC * 33 (24-dot double density)
void printRasterImage(uint8_t* bitmap, int height) {
    // Устанавливаем межстрочный интервал = 24 точки (чтобы полосы стыковались)
    sendCommand(0x1B, 0x33, 24);  // ESC 3 n

    for (int stripY = 0; stripY < height; stripY += 24) {
        // ESC * 33 nL nH — 24-dot double density bit image
        Serial2.write(0x1B);
        Serial2.write(0x2A);      // '*'
        Serial2.write((uint8_t)33);
        Serial2.write((uint8_t)(IMG_WIDTH & 0xFF));         // nL
        Serial2.write((uint8_t)((IMG_WIDTH >> 8) & 0xFF));  // nH

        // Для каждого столбца: 3 байта (24 вертикальные точки)
        for (int x = 0; x < IMG_WIDTH; x++) {
            for (int byteNum = 0; byteNum < 3; byteNum++) {
                uint8_t val = 0;
                for (int bit = 0; bit < 8; bit++) {
                    int y = stripY + byteNum * 8 + bit;
                    if (y < height) {
                        int byteIdx = y * IMG_BYTES_ROW + x / 8;
                        if (bitmap[byteIdx] & (1 << (7 - (x & 7)))) {
                            val |= (1 << (7 - bit));
                        }
                    }
                }
                Serial2.write(val);
            }

            // Сбрасываем буфер каждые 64 столбца
            if ((x & 63) == 63) Serial2.flush();
        }

        Serial2.write(0x0A);  // LF — переход на следующую полосу
        Serial2.flush();
        delay(10);
    }

    // Восстанавливаем межстрочный интервал по умолчанию
    sendCommand(0x1B, 0x32);  // ESC 2
}

// ========== КИРИЛЛИЦА ==========

void setupCyrillic() {
    printerInit();
    delay(200);
    selectCodepage(0x04);
}

uint8_t utf8ToCp1251(uint16_t utf8Code) {
    if (utf8Code >= 0x0410 && utf8Code <= 0x042F)
        return 0xC0 + (utf8Code - 0x0410);
    if (utf8Code >= 0x0430 && utf8Code <= 0x044F)
        return 0xE0 + (utf8Code - 0x0430);
    if (utf8Code == 0x0401) return 0xA8;
    if (utf8Code == 0x0451) return 0xB8;
    return utf8Code & 0xFF;
}

void printCyrillicLine(String text) {
    int i = 0;
    while (i < (int)text.length()) {
        uint8_t c = text[i];
        if ((c & 0x80) == 0) {
            Serial2.write(c);
            i++;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < (int)text.length()) {
            uint16_t code = ((c & 0x1F) << 6) | (text[i+1] & 0x3F);
            Serial2.write(utf8ToCp1251(code));
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < (int)text.length()) {
            uint16_t code = ((c & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F);
            Serial2.write(utf8ToCp1251(code));
            i += 3;
        }
        else i++;
    }
    Serial2.write(0x0A);
    Serial2.flush();
    delay(100);
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ==========

void sendCommand(uint8_t c1, uint8_t c2) {
    Serial2.write(c1); Serial2.write(c2);
    Serial2.flush(); delay(50);
}
void sendCommand(uint8_t c1, uint8_t c2, uint8_t c3) {
    Serial2.write(c1); Serial2.write(c2); Serial2.write(c3);
    Serial2.flush(); delay(50);
}
void printLine(String text) {
    Serial2.print(text); Serial2.write(0x0A);
    Serial2.flush(); delay(100);
}

// ========== ESC/POS ==========

void printerInit()   { sendCommand(0x1B, 0x40); }
void selectCodepage(uint8_t cp) { sendCommand(0x1B, 0x74, cp); }
void setAlignment(uint8_t a)    { sendCommand(0x1B, 0x61, a); }
void setBold(bool on)           { sendCommand(0x1B, 0x45, on ? 1 : 0); }

void feedDots(uint8_t dots) {
    sendCommand(0x1B, 0x4A, dots);
    Serial2.flush(); delay(300);
}
void partialCut() {
    sendCommand(0x1B, 0x6D);
    Serial2.flush(); delay(600);
}

// ========== ТЕСТЫ ==========

void cyrillicTest() {
    setupCyrillic();
    setAlignment(1); setBold(true);
    printCyrillicLine("ТЕСТ КИРИЛЛИЦЫ");
    setBold(false); setAlignment(0);
    printCyrillicLine("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ");
    printCyrillicLine("абвгдеёжзийклмнопрстуфхцчшщъыьэюя");
    printCyrillicLine("Цифры: 0123456789");
    setAlignment(1);
    printCyrillicLine("Привет, мир!");
    feedDots(100); partialCut();
}

void testPrint() {
    printerInit(); delay(200);
    setAlignment(1); setBold(true);
    printLine("=== PRINTER TEST ===");
    setBold(false); setAlignment(0);
    printLine("Nippon NP-F309");
    printLine("ESP32-S3 + RS232 + Telegram");
    printLine("Date: " + String(__DATE__));
    printLine("Time: " + String(__TIME__));
    feedDots(80); partialCut();
}

void shortTest() {
    printerInit(); delay(200);
    printLine("Quick test OK");
    feedDots(60); partialCut();
}

void advancedTest() {
    setupCyrillic();
    setAlignment(1); setBold(true);
    printCyrillicLine("МАГАЗИН ЭЛЕКТРОНИКА");
    setBold(false);
    printCyrillicLine("ул. Примерная, д. 123");
    printCyrillicLine("Тел: +7 (123) 456-78-90");
    printCyrillicLine("------------------------");
    setAlignment(0);
    printCyrillicLine("Arduino UNO      1500 р");
    printCyrillicLine("ESP32-S3          890 р");
    printCyrillicLine("Резистор 10к        5 р");
    printCyrillicLine("Макетка           250 р");
    printCyrillicLine("------------------------");
    setBold(true);
    printCyrillicLine("ИТОГО:           2645 р");
    setBold(false);
    setAlignment(1);
    printCyrillicLine("Спасибо за покупку!");
    feedDots(100); partialCut();
}

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
    Serial2.flush(); delay(500);
}

void printQRCode(String data) {
    Serial2.write(0x1B); Serial2.write(0x71);
    Serial2.write(4); Serial2.write(0); Serial2.write(0); Serial2.write(0);
    uint16_t len = data.length();
    Serial2.write(len & 0xFF); Serial2.write((len >> 8) & 0xFF);
    Serial2.print(data);
    Serial2.flush(); delay(1000);
}

void barcodeQRTest() {
    setupCyrillic();
    setAlignment(1);
    printCyrillicLine("Штрихкод CODE39:");
    setAlignment(0);
    printBarcode("123456789");
    setAlignment(1);
    printCyrillicLine("QR код:");
    printQRCode("https://github.com");
    feedDots(100); partialCut();
}

void frameTest() {
    setupCyrillic();
    const uint8_t W = 48;
    String top = ""; for (uint8_t i=0;i<W;i++) top += "*";
    String mid = "*"; for (uint8_t i=0;i<W-2;i++) mid += " "; mid += "*";

    printCyrillicLine(top);
    for (uint8_t i=0;i<3;i++) printCyrillicLine(mid);
    setAlignment(1);
    sendCommand(0x1B, 0x21, 0x38);
    printCyrillicLine("ТЕСТОВОЕ");
    sendCommand(0x1B, 0x21, 0x00);
    setAlignment(0);
    for (uint8_t i=0;i<3;i++) printCyrillicLine(mid);
    printCyrillicLine(top);
    feedDots(100); partialCut();
}
