// Подключение термопринтера Nippon NP-F309 к ESP32-S3
// RS232 через MAX3232 + поддержка кириллицы CP1251

#define RXD2 17  // Пин RX для Serial2 (подключен к TX MAX3232)
#define TXD2 18  // Пин TX для Serial2 (подключен к RX MAX3232)

void setup() {
    // Инициализация Serial для отладки
    Serial.begin(115200);
    delay(1000);
    Serial.println("Инициализация принтера...");
    
    // Инициализация Serial2 для принтера
    // 9600 baud, 8 data bits, No parity, 1 stop bit
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

    // Увеличение буферов UART для надежности
    Serial2.setRxBufferSize(1024);
    Serial2.setTxBufferSize(1024);
    
    delay(500);
    
    // Настройка кириллицы
    setupCyrillic();
    
    Serial.println("Принтер готов!");
    Serial.println("Команды:");
    Serial.println("  t - полный тест");
    Serial.println("  s - короткий тест");
    Serial.println("  a - расширенный тест (чек на русском)");
    Serial.println("  r - тест кириллицы");
    Serial.println("  b - тест штрихкода и QR");
    Serial.println("  f - рамка из * с текстом ТЕСТОВОЕ");
}

void loop() {
    // Ожидание команд через Serial Monitor
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch(cmd) {
            case 't':
                testPrint();
                break;
            case 's':
                shortTest();
                break;
            case 'a':
                advancedTest();
                break;
            case 'r':
                cyrillicTest();
                break;
            case 'b':
                barcodeQRTest();
                break;
            case 'f':
                frameTest();
                break;
            default:
                Serial.println("Неизвестная команда");
        }
    }
}

// ========== НАСТРОЙКА КИРИЛЛИЦЫ ==========

void setupCyrillic() {
    Serial.println("Настройка кириллицы CP1251...");
    
    // Инициализация принтера
    printerInit();  // ESC @
    delay(200);
    
    // Выбор кодовой таблицы CP1251 (кириллица)
    // ESC t n, где n=04 это CP1251
    selectCodepage(0x04);
    
    Serial.println("CP1251 активирована!");
}

// ========== ТАБЛИЦА ПЕРЕКОДИРОВКИ UTF-8 -> CP1251 ==========

// Преобразование UTF-8 символа в CP1251
uint8_t utf8ToCp1251(uint16_t utf8Code) {
    // Таблица соответствия UTF-8 -> CP1251 для кириллицы
    // UTF-8 кириллица: 0x0410-0x044F
    // CP1251 кириллица: 0xC0-0xFF
    
    if (utf8Code >= 0x0410 && utf8Code <= 0x042F) {
        // Заглавные буквы А-Я (0x0410-0x042F -> 0xC0-0xDF)
        return 0xC0 + (utf8Code - 0x0410);
    }
    else if (utf8Code >= 0x0430 && utf8Code <= 0x044F) {
        // Строчные буквы а-я (0x0430-0x044F -> 0xE0-0xFF)
        return 0xE0 + (utf8Code - 0x0430);
    }
    else if (utf8Code == 0x0401) {
        // Ё заглавная
        return 0xA8;
    }
    else if (utf8Code == 0x0451) {
        // ё строчная
        return 0xB8;
    }
    
    // Если не кириллица, возвращаем как есть (ASCII)
    return utf8Code & 0xFF;
}

// Печать строки с поддержкой кириллицы
void printCyrillicLine(String text) {
    int i = 0;
    while (i < text.length()) {
        uint8_t c = text[i];
        
        // Проверка на UTF-8 многобайтовый символ
        if ((c & 0x80) == 0) {
            // ASCII символ (0x00-0x7F)
            Serial2.write(c);
            i++;
        }
        else if ((c & 0xE0) == 0xC0) {
            // 2-байтовый UTF-8 символ (110xxxxx 10xxxxxx)
            if (i + 1 < text.length()) {
                uint8_t c2 = text[i + 1];
                uint16_t utf8Code = ((c & 0x1F) << 6) | (c2 & 0x3F);
                uint8_t cp1251 = utf8ToCp1251(utf8Code);
                Serial2.write(cp1251);
                i += 2;
            } else {
                i++;
            }
        }
        else if ((c & 0xF0) == 0xE0) {
            // 3-байтовый UTF-8 символ (1110xxxx 10xxxxxx 10xxxxxx)
            if (i + 2 < text.length()) {
                uint8_t c2 = text[i + 1];
                uint8_t c3 = text[i + 2];
                uint16_t utf8Code = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                uint8_t cp1251 = utf8ToCp1251(utf8Code);
                Serial2.write(cp1251);
                i += 3;
            } else {
                i++;
            }
        }
        else {
            // Неизвестный формат, пропускаем
            i++;
        }
    }
    
    // Перевод строки
    Serial2.write(0x0A);  // Line Feed
    Serial2.flush();
    delay(100);
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

// Отправка команды из 2 байт
void sendCommand(uint8_t cmd1, uint8_t cmd2) {
    Serial2.write(cmd1);
    Serial2.write(cmd2);
    Serial2.flush();
    delay(50);
}

// Отправка команды из 3 байт
void sendCommand(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3) {
    Serial2.write(cmd1);
    Serial2.write(cmd2);
    Serial2.write(cmd3);
    Serial2.flush();
    delay(50);
}

// Печать строки текста БЕЗ кириллицы (только ASCII)
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

// Выравнивание текста
void setAlignment(uint8_t align) {
    // align: 0=left, 1=center, 2=right
    sendCommand(0x1B, 0x61, align);
}

// Жирный шрифт
void setBold(bool enable) {
    sendCommand(0x1B, 0x45, enable ? 1 : 0);
}

// ========== ТЕСТОВЫЕ ФУНКЦИИ ==========

// Тест кириллицы с УВЕЛИЧЕННОЙ подачей
void cyrillicTest() {
    Serial.println("Тест кириллицы...");
    
    setupCyrillic();  // Переключаем на CP1251
    
    setAlignment(1);  // Центр
    setBold(true);
    printCyrillicLine("ТЕСТ КИРИЛЛИЦЫ");
    setBold(false);
    
    setAlignment(0);  // Влево
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
    
    // УВЕЛИЧЕННАЯ ПОДАЧА - 100 точек (~12.5мм)
    Serial.println("Подача бумаги 100 точек...");
    feedDots(100);  // 100 точек (~12.5мм)
    
    // Частичный отрез
    Serial.println("Отрезка...");
    partialCut();
    
    Serial.println("Тест кириллицы завершен!");
}

// Полный тест (английский)
void testPrint() {
    Serial.println("Запуск тестовой печати...");
    
    printerInit();  // Инициализация
    delay(200);
    
    setAlignment(1);  // Центр
    setBold(true);
    printLine("=== PRINTER TEST ===");
    setBold(false);
    
    setAlignment(0);  // Влево
    printLine("Nippon NP-F309");
    printLine("ESP32-S3 + RS232");
    printLine("Date: " + String(__DATE__));
    printLine("Time: " + String(__TIME__));
    
    setAlignment(1);
    printLine("====================");
    
    // Подача 80 точек
    feedDots(80);  // 80 точек (~10мм)
    
    // Частичный отрез
    partialCut();
    
    Serial.println("Печать завершена!");
}

// Короткий тест
void shortTest() {
    Serial.println("Короткий тест...");
    
    printerInit();  // Инициализация
    delay(200);
    
    printLine("Quick test OK");
    
    // Подача 60 точек
    feedDots(60);  // 60 точек (~7.5мм)
    
    // Частичный отрез
    partialCut();
    
    Serial.println("Готово!");
}

// Расширенный тест с кириллицей и УВЕЛИЧЕННОЙ подачей
void advancedTest() {
    Serial.println("Расширенный тест (чек на русском)...");
    
    setupCyrillic();  // Включаем кириллицу
    
    // Заголовок
    setAlignment(1);  // Центр
    setBold(true);
    printCyrillicLine("МАГАЗИН ЭЛЕКТРОНИКА");
    setBold(false);
    printCyrillicLine("ул. Примерная, д. 123");
    printCyrillicLine("Тел: +7 (123) 456-78-90");
    printCyrillicLine("------------------------");
    
    // Товары
    setAlignment(0);  // Влево
    printCyrillicLine("Товар             Цена");
    printCyrillicLine("------------------------");
    printCyrillicLine("Arduino UNO      1500 р");
    printCyrillicLine("ESP32-S3          890 р");
    printCyrillicLine("Резистор 10к        5 р");
    printCyrillicLine("Макетка           250 р");
    printCyrillicLine("------------------------");
    
    // Итог
    setBold(true);
    printCyrillicLine("ИТОГО:           2645 р");
    setBold(false);
    
    printCyrillicLine("------------------------");
    setAlignment(1);  // Центр
    printCyrillicLine("Спасибо за покупку!");
    printLine(String(__DATE__) + " " + String(__TIME__));
    
    // УВЕЛИЧЕННАЯ ПОДАЧА - 100 точек (~12.5мм)
    Serial.println("Подача бумаги 100 точек...");
    feedDots(100);  // 100 точек (~12.5мм)
    
    // Частичный отрез
    Serial.println("Отрезка...");
    partialCut();
    
    Serial.println("Чек распечатан!");
}

// ========== ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ ==========

// Печать штрих-кода (пример для CODE39)
void printBarcode(String data) {
    // Высота штрихкода
    Serial2.write(0x1D);  // GS
    Serial2.write(0x68);  // h
    Serial2.write(100);   // 100 точек высота
    Serial2.flush();
    delay(50);
    
    // Ширина модуля
    Serial2.write(0x1D);  // GS
    Serial2.write(0x77);  // w
    Serial2.write(3);     // Ширина 3
    Serial2.flush();
    delay(50);
    
    // Позиция HRI символов (2=снизу)
    Serial2.write(0x1D);  // GS
    Serial2.write(0x48);  // H
    Serial2.write(2);     // Снизу
    Serial2.flush();
    delay(50);
    
    // Печать штрихкода CODE39
    Serial2.write(0x1D);  // GS
    Serial2.write(0x6B);  // k
    Serial2.write(0x04);  // m = CODE39
    
    // Данные (должны начинаться и заканчиваться на *)
    Serial2.print("*" + data + "*");
    Serial2.write(0x00);  // NUL - конец данных
    Serial2.flush();
    delay(500);
}

// Печать QR-кода
void printQRCode(String data) {
    Serial2.write(0x1B);  // ESC
    Serial2.write(0x71);  // q
    Serial2.write(4);     // S - размер модуля (4 точки)
    Serial2.write(0);     // E - уровень коррекции (0=L)
    Serial2.write(0);     // V - версия (0=авто)
    Serial2.write(0);     // M - маска (0=оптимальная)
    
    // Длина данных (n1 + n2*256)
    uint16_t len = data.length();
    Serial2.write(len & 0xFF);        // n1
    Serial2.write((len >> 8) & 0xFF); // n2
    
    // Данные
    Serial2.print(data);
    Serial2.flush();
    delay(1000);  // QR-коду нужно больше времени
}

// Пример использования штрихкода и QR-кода
void barcodeQRTest() {
    Serial.println("Тест штрихкода и QR...");
    
    setupCyrillic();
    
    setAlignment(1);
    printCyrillicLine("Штрихкод CODE39:");
    setAlignment(0);
    printBarcode("123456789");  // Печать штрихкода
    
    setAlignment(1);
    printCyrillicLine("");
    printCyrillicLine("QR код:");
    printQRCode("https://github.com");  // Печать QR
    
    // Подача и отрез
    feedDots(100);
    partialCut();
    
    Serial.println("Готово!");
}


// Режим печати квадратной рамки из символа *
void frameTest() {
    Serial.println("Печать рамки из *...");

    setupCyrillic();

    const uint8_t lineWidth = 48;  // Font A: 48 символов на строку (576 точек / 12)
    String topBottom = "";
    for (uint8_t i = 0; i < lineWidth; i++) {
        topBottom += "*";
    }

    String middle = "*";
    for (uint8_t i = 0; i < lineWidth - 2; i++) {
        middle += " ";
    }
    middle += "*";

    // Строка с текстом в режиме двойной ширины+высоты+жирный
    // ESC ! n: bit3=жирный, bit4=двойная высота, bit5=двойная ширина = 0x38
    // При двойной ширине на строку помещается 24 символа
    const uint8_t dwLineWidth = 24;
    String text = "ТЕСТОВОЕ";
    uint8_t textLen = 8;
    String textLine = "*";
    uint8_t innerWidth = dwLineWidth - 2;  // 22
    uint8_t leftPad = (innerWidth - textLen) / 2;   // 7
    uint8_t rightPad = innerWidth - textLen - leftPad;
    for (uint8_t i = 0; i < leftPad; i++) textLine += " ";
    textLine += text;
    for (uint8_t i = 0; i < rightPad; i++) textLine += " ";
    textLine += "*";

    printCyrillicLine(topBottom);
    for (uint8_t i = 0; i < 3; i++) {
        printCyrillicLine(middle);
    }

    // Включаем двойную ширину + двойную высоту + жирный
    sendCommand(0x1B, 0x21, 0x38);
    printCyrillicLine(textLine);
    // Возврат к обычному режиму
    sendCommand(0x1B, 0x21, 0x00);

    for (uint8_t i = 0; i < 3; i++) {
        printCyrillicLine(middle);
    }
    printCyrillicLine(topBottom);

    feedDots(100);
    partialCut();

    Serial.println("Рамка распечатана!");
}
