/* mbed Microcontroller Library
 * Copyright (c) 2019-2023 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <time.h>

#include "mbed.h"
#include "USBSerial.h"
#include "SB1602E.h"
#include "RCS620S.h"
#include "AS289R2.h"
#include "sc_utf8.h"

// RCS620S
#define PUSH_TIMEOUT                  2100
#define COMMAND_TIMEOUT               400
#define POLLING_INTERVAL              500ms
#define RCS620S_MAX_CARD_RESPONSE_LEN 30
 
// FeliCa Service/System Code
#define CYBERNE_SYSTEM_CODE           0x0003
#define COMMON_SYSTEM_CODE            0xFE00
#define PASSNET_SERVICE_CODE          0x090F
#define FELICA_ATTRIBUTE_CODE         0x008B
#define KITACA_SERVICE_CODE           0x208B
#define TOICA_SERVICE_CODE            0x1E8B
#define GATE_SERVICE_CODE             0x184B
#define PASMO_SERVICE_CODE            0x1cc8

#define EDY_ATTRIBUTE_CODE            0x110B
#define EDY_SERVICE_CODE              0x1317
#define EDY_HISTORY_CODE              0x170F

#define NANACO_SERVICE_CODE           0x564F
#define NANACO_ID_CODE                0x558B
#define NANACO_BALANCE_CODE           0x5597
#define NANACO_POINT_CODE             0x560B

#define WAON_SERVICE_CODE0            0x680B
#define WAON_SERVICE_CODE1            0x6817
#define WAON_SERVICE_CODE2            0x684B
#define WAON_SERVICE_ID               0x684F

#define PRINT_ENTRIES                 20    // Max. 20

#define SWAP(type,a,b)          { type work = a; a = b; b = work; }

int requestService(uint16_t serviceCode);
int readEncryption(uint16_t serviceCode, uint8_t blockNumber, uint8_t *buf);
void printBalanceLCD(const char *card_name, uint32_t balance);
void parse_history_suica(uint8_t *buf);
void parse_history_nanaco(uint8_t *buf);
void parse_history_waon(uint8_t *buf);
void parse_history_edy(uint8_t *buf);
void get_station_name(char *buf, int area, int line, int station);

const uint32_t record_length = (3 + 40 + 40);

DigitalOut led(LED1);
USBSerial serial(false);
SB1602E lcd(I2C_LCD_SDA, I2C_LCD_SCL);
RCS620S rcs620s(RCS620S_TX, RCS620S_RX);
AS289R2 tp(AS289R2_TX, AS289R2_RX);

int main()
{
    uint8_t buffer[20][16];
    uint8_t idm[8];
    uint8_t attr[RCS620S_MAX_CARD_RESPONSE_LEN];

    DigitalIn mode(BOOT_PIN, PullUp);

    lcd.setCharsInLine(8);
    lcd.clear();
    lcd.contrast(0x35);

    ThisThread::sleep_for(2000ms);

    if (mode.read() == 0) {
        lcd.printf(0, 0, (char*)"Waiting");
        lcd.printf(0, 1, (char*)"USB...");
        serial.init();
        serial.connect();
        while(serial.connected() == false) {
            ThisThread::sleep_for(10ms);
        }
    }

    lcd.printf(0, 0, (char*)"FeliCa");
    lcd.printf(0, 1, (char*)"Reader");

    serial.printf("\n*** RCS620S FeliCaリーダープログラム ***\n\n");

    rcs620s.initDevice();
    tp.initialize();
    tp.putLineFeed(1);
    memset(idm, 0, 8);

    while (1) {
        uint32_t balance = 0;
        uint8_t buf[RCS620S_MAX_CARD_RESPONSE_LEN];
        int isCaptured = 0;
        
        rcs620s.timeout = COMMAND_TIMEOUT;
        
        // サイバネ領域
        if (rcs620s.polling(CYBERNE_SYSTEM_CODE)) {
            // Suica, PASMO等の交通系ICカード
            if (requestService(PASSNET_SERVICE_CODE)) {
                for (int i = 0; i < 20; i++) {
                    if (readEncryption(PASSNET_SERVICE_CODE, i, buf)) {
                        memcpy(buffer[i], &buf[12], 16);
                    }
                }
                if (memcmp(idm, buf + 1, 8) != 0) {
                    // カード変更
                    isCaptured = 1;
                    memcpy(idm, buf + 1, 8);
#if 1
                    char info[40];
                    snprintf(info, sizeof(info), "IDm: %02x%02x-%02x%02x-%02x%02x-%02x%02x", idm[0], idm[1], idm[2], idm[3], idm[4], idm[5], idm[6], idm[7]);
                    serial.printf("%s\n", info);
#endif
                }
                else {
                    // 前と同じカード
                    isCaptured = 0;
                }
            }
            if (isCaptured) {
                if (requestService(FELICA_ATTRIBUTE_CODE)) {
                    readEncryption(FELICA_ATTRIBUTE_CODE, 0, attr);
                }

                // 残高取得
                balance = buffer[0][11];                  // 11 byte目
                balance = (balance << 8) + buffer[0][10]; // 10 byte目

                // カード種別
                char card[8];
                if ((attr[12+8] & 0xF0) == 0x30) {
                    strcpy(card, "ICOCA");
                }
                else {
                    if (requestService(KITACA_SERVICE_CODE)) {
                        strcpy(card, "Kitaca");
                    }
                    else if (requestService(TOICA_SERVICE_CODE)) {
                        strcpy(card, "toica");
                    }
                    else if (requestService(GATE_SERVICE_CODE) != 1) {
                        strcpy(card, "SUGOCA");
                    }
                    else if (requestService(PASMO_SERVICE_CODE)) {
                        strcpy(card, "PASMO");
                    }                    
                    else {
                        strcpy(card, "Suica");
                    }
                }

                // 残高表示
                printBalanceLCD(card , balance);
                for (int i = (PRINT_ENTRIES - 1); i >= 0; i--) {
                    if (buffer[i][0] != 0) {
                        parse_history_suica(&buffer[i][0]);
                    }
                }
                tp.putLineFeed(4);
                isCaptured = 0;
            }
        }
        
        // 共通領域
        else if (rcs620s.polling(COMMON_SYSTEM_CODE)){
            // Edy
            if (requestService(EDY_ATTRIBUTE_CODE) && readEncryption(EDY_ATTRIBUTE_CODE, 0, buf)) {                    
                if (memcmp(idm, &buf[12 + 2], 8) != 0) {
                    isCaptured = 1;
                    memcpy(idm, &buf[12 + 2], 8);
                }
                else {
                    isCaptured = 0;
                }
                if (requestService(EDY_ATTRIBUTE_CODE) && readEncryption(EDY_ATTRIBUTE_CODE, 0, buf) && isCaptured) {                    
                    char info[80];
                    snprintf(info, sizeof(info), "Edy ID: %02x%02x-%02x%02x-%02x%02x-%02x%02x", buf[12+2], buf[12+3], buf[12+4], buf[12+5], buf[12+6], buf[12+7], buf[12+8], buf[12+9]);
                    serial.printf("%s\n", info);
                    tp.printf("%s\r", info);
                    
                    int tmp;
                    tmp = buf[12 + 10];
                    int day = ((tmp << 8) + buf[12+11]) >> 1;
                    int sec = ((buf[12 + 11] & 1) << 16) + (buf[12 + 12] << 8) + buf[12 + 13];

                    struct tm tm, *pt;
                    time_t t = (time_t)(-1);
                    tm.tm_year = 2000 - 1900;
                    tm.tm_mon = 1 - 1;
                    tm.tm_mday = 1 + day;
                    tm.tm_hour = 0;
                    tm.tm_min = 0;
                    tm.tm_sec = sec;
                    t = mktime(&tm);
                    pt = localtime(&t);
                    snprintf(info, sizeof(info), "発行日: %d年%d月%d日 %02d:%02d", pt->tm_year+1900, pt->tm_mon+1, pt->tm_mday, pt->tm_hour, pt->tm_min);
                    serial.printf("%s\n", info);
                    tp.printf("%s\r", info);
                }
                if (requestService(EDY_SERVICE_CODE) && readEncryption(EDY_SERVICE_CODE, 0, buf) && isCaptured) {
                    balance = buf[12 + 0];
                    balance += (buf[12 + 1] << 8);
                    balance += (buf[12 + 2] << 8);
                    balance += (buf[12 + 3] << 8);
                    printBalanceLCD("Edy", balance);
                }
                if (isCaptured) {
                    for (int i = 0; i < 6; i++) {
                        if (readEncryption(EDY_HISTORY_CODE, i, buf) && isCaptured) {
                            memcpy(buffer[i], &buf[12], 16);
                        }
                    }
                    for (int i = 5; i >= 0; i--) {
                        if (isCaptured) {
                            parse_history_edy(&buffer[i][0]);
                        }
                    }
                    tp.setDoubleSizeWidth();
                    tp.printf("\r残高 %ld円\r\r", balance);
                    tp.clearDoubleSizeWidth();
                    tp.putLineFeed(3);
                }
            }
            
            // nanaco
            if (requestService(NANACO_ID_CODE) && readEncryption(NANACO_ID_CODE, 0, buf)) {
                if (memcmp(idm, &buf[12], 8) != 0) {
                    uint32_t point;
                    char info[40];
                    memcpy(idm, &buf[12], 8);
                    snprintf(info, sizeof(info), "nanaco ID: %02x%02x-%02x%02x-%02x%02x-%02x%02x", buf[12], buf[13], buf[14], buf[15], buf[16], buf[17], buf[18], buf[19]);
                    serial.printf("%s\n", info);
                    tp.printf("%s\r", info);
                    readEncryption(NANACO_POINT_CODE, 1, buf);
                    point = buf[12 + 1];
                    point = (point << 8) + buf[12 + 2];
                    snprintf(info, sizeof(info), "nanacoポイント: %ldpt", point);
                    serial.printf("%s\n\n", info);
                    tp.printf("%s\r\r", info);
                    isCaptured = 1;
                }
                else {
                    isCaptured = 0;
                }
            }
#if 0
            if (requestService(NANACO_POINT_CODE) && readEncryption(NANACO_POINT_CODE, 1, buf) && isCaptured) {
                uint32_t point;
                char info[40];
                point = buf[12 + 1];
                point = (point << 8) + buf[12 + 2];
                snprintf(info, sizeof(info), "nanacoポイント: %ldpt", point);
                serial.printf("%s\n\n", info);
                tp.printf("%s\r\r", info);
            }
#endif
            if (requestService(NANACO_BALANCE_CODE) && readEncryption(NANACO_BALANCE_CODE, 0, buf) && isCaptured) {
                // Little Endianで入っているnanacoの残高を取り出す
                balance = buf[12 + 0];
                balance += (buf[12 + 1] << 8);
                balance += (buf[12 + 2] << 8);
                balance += (buf[12 + 3] << 8);
                // 残高表示
                printBalanceLCD("nanaco", balance);
                
                for (int i = 5; i > 0; i--) {
                    if (readEncryption(NANACO_SERVICE_CODE, i-1, buf)) {
                        parse_history_nanaco(buf);
                    }
                }
                tp.printf("\r");
                tp.setDoubleSizeWidth();
                tp.printf("\r残高 %ld円\r\r", balance);
                tp.clearDoubleSizeWidth();
                tp.putLineFeed(3);
            }
            
            // waon
            if (requestService(WAON_SERVICE_ID) && readEncryption(WAON_SERVICE_ID, 0, buf)) {
                if (memcmp(idm, &buf[12], 8) != 0) {
                    char info[40];
                    isCaptured = 1;
                    memcpy(idm, &buf[12], 8);
                    snprintf(info, sizeof(info), "WAON ID: %02x%02x-%02x%02x-%02x%02x-%02x%02x", buf[12], buf[13], buf[14], buf[15], buf[16], buf[17], buf[18], buf[19]);
                    serial.printf("%s\n\n", info);
                    tp.printf("%s\r\r", info);
                }
                else {
                    isCaptured = 0;
                }
            }
            if (requestService(WAON_SERVICE_CODE1) && readEncryption(WAON_SERVICE_CODE1, 0, buf) && isCaptured) {
                // Little Endianで入っているwaonの残高を取り出す
                balance = buf[13];
                balance = (balance << 8) + buf[12];
                // 残高表示
                printBalanceLCD("waon", balance);

                parse_history_waon(buf);

                serial.printf("残高 %ld円\n", balance);
                tp.setDoubleSizeWidth();
                tp.printf("残高 %ld円\r\r", balance);
                tp.clearDoubleSizeWidth();
                tp.putLineFeed(3);

            }
        }
        rcs620s.rfOff();
        led = !led;
        ThisThread::sleep_for(POLLING_INTERVAL);
    }
}

void parse_history_suica(uint8_t *buf)
{
    char info[80+80+4], info2[40+40];
    int region_in, region_out, line_in, line_out, station_in, station_out;

    region_in = (buf[0xf] >> 6) & 3;
    region_out = (buf[0xf] >> 4) & 3;
    line_in = buf[6];
    line_out = buf[8];
    station_in = buf[7];
    station_out = buf[9];

    serial.printf("\n");
    for (int i = 0; i < 16; i++) {
        serial.printf("%02X ", buf[i]);
    }
    serial.printf("\n");

    snprintf(info, sizeof(info), "機種種別: ");
    switch (buf[0]) {
        case 0x03:
            strcat(info, "のりこし精算機\r");
            break;
        case 0x04:
            strcat(info, "携帯型端末\r");
            break;
        case 0x05:
            strcat(info, "バス/路面等\r");
            break;
        case 0x09:
            strcat(info, "入金機\r");
            break;
        case 0x07:
        case 0x08:
        case 0x12:
            strcat(info, "券売機\r");
            break;
        case 0x14:
        case 0x15:
            strcat(info, "券売機等\r");
            break;
        case 0x16:
            strcat(info, "自動改札機\r");
            break;
        case 0x17:
            strcat(info, "簡易改札機\r");
            break;
        case 0x18:
        case 0x19:
            strcat(info, "窓口端末\r");
            break;
        case 0x1A:
            strcat(info, "改札端末\r");
            break;
        case 0x1B:
            strcat(info, "モバイルFeliCa\r");
            break;
        case 0x1C:
            strcat(info, "乗継精算機\r");
            break;
        case 0x1D:
            strcat(info, "連絡改札機\r");
            break;
        case 0x1F:
            strcat(info, "簡易入金機\r");
            break;
        case 0x23:
            strcat(info, "乗継精算機\r");
            break;
        case 0x46:
        case 0x48:
            strcat(info, "ビューアルッテ端末\r");
            break;
        case 0xc7:
        case 0xc9:
            strcat(info, "物販端末\r");
            break;
        case 0xc8:
            strcat(info, "自販機\r");
            break;
        default:
            strcat(info, "不明\r");
            break;
    }
    serial.printf("%s", info);
    //tp.printf("%s", info);

    int hasStationName = 0;
    snprintf(info, sizeof(info), "利用種別: ");
    switch (buf[1]) {
        case 0x01:
            strcat(info, "自動改札出場");
            break;
        case 0x02:
            strcat(info, "SFチャージ\r");
            hasStationName = 1;
            break;
        case 0x03:
            strcat(info, "きっぷ購入\r");
            hasStationName = 1;
            break;
        case 0x04:
            strcat(info, "磁気券精算");
            break;
        case 0x05:
            strcat(info, "乗越精算");
            break;
        case 0x06:
            strcat(info, "窓口精算");
            break;
        case 0x07:
            strcat(info, "新規\r");
            hasStationName = 1;
            break;
        case 0x08:
            strcat(info, "チャージ控除");
            break;
        case 0x0C:
        case 0x0D:
        case 0x0F:
            strcat(info, "バス/路面等");
            break;
        case 0x13:
            strcat(info, "支払い（新幹線利用）\r");
            hasStationName = 2;
            break;
        case 0x14:
        case 0x15:
            strcat(info, "オートチャージ");
            break;
        case 0x46:
            strcat(info, "物販");
            break;
        case 0xc6:
            strcat(info, "現金併用物販");
            break;
        default:
            strcat(info, "不明");
            break;
    }
    if (hasStationName >= 1) {
        get_station_name(info2, region_in, line_in, station_in);
        strcat(info, info2);
    }
    if (hasStationName == 2) {
        strcat(info, " - ");
        get_station_name(info2, region_out, line_out, station_out);
        strcat(info, info2);
    }
    serial.printf("%s\r", info);
    tp.printf("%s\r", info);

#if 0
    if (buf[2] != 0) {
        snprintf(info, sizeof(info), "支払種別: ");
        switch (buf[2]) {
            case 0x02:
                strcat(info, "VIEW\r");
                break;
            case 0x0B:
                strcat(info, "PiTaPa\r");
                break;
            case 0x0d:
                strcat(info, "オートチャージ対応PASMO\r");
                break;
            case 0x3f:
                strcat(info, "モバイルSuica\r");
                break;
            default:
                strcat(info, "不明\r");
                break;
        }
        serial.printf("%s", info);
        tp.printf("%s", info);
    }
#endif

    hasStationName = 0;
    if (buf[1] == 0x01 || buf[1] == 0x14) {
        snprintf(info, sizeof(info), "入出場種別: ");
        switch (buf[3]) {
            case 0x01:
            case 0x08:
                strcat(info, "入場\r");
                hasStationName = 1;
                break;
            case 0x02:
                strcat(info, "出場\r");
                hasStationName = 2;
                break;
            case 0x03:
                strcat(info, "定期入場\r");
                hasStationName = 1;
                break;
            case 0x04:
                strcat(info, "定期出場\r");
                hasStationName = 2;
                break;
            case 0x0E:
                strcat(info, "窓口出場");
                break;
            case 0x0F:
                strcat(info, "バス入出場");
                break;
            case 0x12:
                strcat(info, "料金定期");
                break;
            case 0x17:
            case 0x1D:
                strcat(info, "乗継割引\r");
                hasStationName = 2;
                break;
            case 0x21:
                strcat(info, "バス等乗継割引");
                break;
            case 0x22:
            case 0x25:
            case 0x26:
                strcat(info, "券面外乗降\r");
                hasStationName = 2;
                break;
            default:
                strcat(info, "不明");
                snprintf(info2, sizeof(info2), " %02X %02X %02X %02X", buf[6], buf[7], buf[8], buf[9]);
                strcat(info, info2);
                break;
        }
        if (hasStationName >= 1) {
            get_station_name(info2, region_in, line_in, station_in);
            strcat(info, info2);
        }
        if (hasStationName == 2) {
            strcat(info, " - ");
            get_station_name(info2, region_out, line_out, station_out);
            strcat(info, info2);
        }
        serial.printf("%s\r", info);
        tp.printf("%s\r", info);
    }

    snprintf(info, sizeof(info), "処理日付: %d/%02d/%02d", 2000+(buf[4]>>1), ((buf[4]&1)<<3 | ((buf[5]&0xe0)>>5)), buf[5]&0x1f);
    if (buf[1] == 0x46 || buf[1] == 0xc6) {   // 物販
        snprintf(info2, sizeof(info2), " %02d:%02d:%02d", (buf[6] & 0xF8) >> 3, ((buf[6] & 0x7) << 3) | ((buf[7] & 0xe0) >> 5), (buf[7] & 0x1f));
        strcat(info, info2);
    }
    strcat(info, "\r");
    serial.printf("%s", info);
    tp.printf("%s", info);

    snprintf(info, sizeof(info), "残額: %d円", (buf[11]<<8) + buf[10]); 
    serial.printf("%s\n", info);
    tp.setDoubleSizeWidth();
    tp.printf("%s\r\r", info);
    tp.clearDoubleSizeWidth();
}

void parse_history_nanaco(uint8_t *buf)
{
    char info[80], info2[10];
    uint16_t tmp;

    if (buf[12] == 0) {
        return;
    }

#if 1
    serial.printf("\n");
    for (int i = 0; i < RCS620S_MAX_CARD_RESPONSE_LEN-2; i++) {
        serial.printf("%02X ", buf[i]);
    }
    serial.printf("\n");
#else
    serial.printf("-----\n");
    tp.printf("\r");
#endif
    snprintf(info, sizeof(info), "種別: ");
    if (buf[12] == 0x35) {
        strcat(info, "引継");
    }
    if (buf[12] == 0x47) {
        strcat(info, "支払い");
    }
    if (buf[12] == 0x6F || buf[12] == 0x70) {
        strcat(info, "チャージ");
    }
    if (buf[12] == 0x7A) {
        strcat(info, "新規");
    }
    if (buf[12] == 0x83) {
        strcat(info, "ポイント交換チャージ");
    }
    serial.printf("%s\n", info);
    tp.printf("%s\r", info);
    
    tmp = buf[21];
    tmp = (tmp << 8) + buf[22];
    tmp = (tmp >> 5) & 0x07FF;
    snprintf(info, sizeof(info), "日時: %d年", 2000+tmp);
    
    tmp = buf[22] & 0x1E;
    tmp = (tmp >> 1);
    snprintf(info2, sizeof(info2), "%02d月", tmp);
    strcat(info, info2);

    tmp = buf[22];
    tmp = (tmp << 8) + buf[23];
    tmp = (tmp >> 4) & 0x001F;
    snprintf(info2, sizeof(info2), "%02d日 ", tmp);
    strcat(info, info2);

    tmp = buf[23];
    tmp = (tmp << 8) + buf[24];
    tmp = (tmp >> 6) & 0x3F;
    snprintf(info2, sizeof(info2), "%02d:", tmp);
    strcat(info, info2);

    tmp = buf[24];
    tmp = tmp & 0x3F;
    snprintf(info2, sizeof(info2), "%02d", tmp);
    strcat(info, info2);
    serial.printf("%s\r", info);
    tp.printf("%s\r", info);

    tmp = buf[15];
    tmp = (tmp << 8) + buf[16];
    snprintf(info, sizeof(info), "取扱金額: %d円", tmp);
    serial.printf("%s\r", info);
    tp.printf("%s\r", info);

    tmp = buf[19];
    tmp = (tmp << 8) + buf[20];
    snprintf(info, sizeof(info), "残高: %d円", tmp);
    serial.printf("%s\r", info);
    tp.printf("%s\r", info);
}

void parse_history_waon(uint8_t *buf)
{
    char info[200], info2[50];
    uint32_t num[3] = {0};
    int array[3] = {0, 2, 4};
    uint32_t tmp;
    
#if 0
    serial.printf("\n");
    for (int i = 0; i < RCS620S_MAX_CARD_RESPONSE_LEN-2; i++) {
        serial.printf("%02X ", buf[i]);
    }
    serial.printf("\n");
#endif

    for (int i = 0; i < 3; i += 2) {
        if (readEncryption(WAON_SERVICE_CODE0, i*2, buf)) {
            num[i] = buf[12 + 13];
            num[i] = (num[i] << 8) + buf[12 + 14];
        }
    }

    for (int i = 0; i < 3 - 1; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (num[i] > num[j]) {
                SWAP(int, array[i], array[j]);
            }
        }
    }

    int next_valid = 0;
    for (int i = 0; i < 3; i++) {
        if (readEncryption(WAON_SERVICE_CODE0, array[i], buf)) {
            tmp = buf[12 + 13];
            tmp = (tmp << 8) + buf[12 + 14];
            if (tmp != 0) {
                serial.printf("------\n");
                snprintf(info, sizeof(info), "端末番号: ");
                for (int ch = 0; ch <= 12; ch++) {
                    snprintf(info2, sizeof(info2), "%c", buf[12 + ch]);
                    strcat(info, info2);
                }
                snprintf(info2, sizeof(info2), " (%ld)\r", tmp);
                strcat(info, info2);
                serial.printf("%s", info);
                tp.printf("%s", info);
                next_valid = 1;
            }
            else {
                next_valid = 0;
            }
        }
        if (next_valid && readEncryption(WAON_SERVICE_CODE0, array[i] + 1, buf)) {
            snprintf(info, sizeof(info), "種別: ");
            tmp = buf[12 + 1];
            switch (tmp) {
                case 0x04:
                    snprintf(info2, sizeof(info2), "支払い");
                    break;
                case 0x08:
                    snprintf(info2, sizeof(info2), "返品");
                    break;
                case 0x0C:
                    snprintf(info2, sizeof(info2), "チャージ(現金、ポイントチャージ)");
                    break;
                case 0x10:
                    snprintf(info2, sizeof(info2), "チャージ");
                    break;
                case 0x18:
                    snprintf(info2, sizeof(info2), "ポイントダウンロード");
                    break;
                case 0x28:
                    snprintf(info2, sizeof(info2), "返金");
                    break;
                case 0x1C:
                case 0x20:
                    snprintf(info2, sizeof(info2), "購入時にオートチャージ");
                    break;
                case 0x30:
                    snprintf(info2, sizeof(info2), "オートチャージ(銀行)");
                    break;
                case 0x3C:
                    snprintf(info2, sizeof(info2), "新カードへの移行");
                    break;
                case 0x7C:
                    snprintf(info2, sizeof(info2), "ポイント交換(預入)");
                    break;
            }
            strcat(info, info2);
            serial.printf("%s\n", info);
            tp.printf("%s\r", info);

            tmp = buf[12 + 2];
            tmp = ((tmp >> 3) & 0x1F) + 2005;
            snprintf(info, sizeof(info), "日時: %ld年", tmp);

            tmp = buf[12 + 2];
            tmp = ((tmp & 0x7) << 1) + ((buf[12 + 3] >> 7 ) & 0x01);
            snprintf(info2, sizeof(info2), "%2ld月", tmp);
            strcat(info, info2);

            tmp = ((buf[12 + 3] >> 2) & 0x1F);
            snprintf(info2, sizeof(info2), "%2ld日 ", tmp);
            strcat(info, info2);

            tmp = ((buf[12 + 3] << 3 ) & 0x18);
            tmp = tmp + ((buf[12 + 4] >> 5) & 0x7);
            snprintf(info2, sizeof(info2), "%02ld:", tmp);
            strcat(info, info2);

            tmp = (buf[12 + 4] & 0x1F);
            tmp = (tmp << 1) + ((buf[12 + 5] >> 7) & 0x01);
            snprintf(info2, sizeof(info2), "%02ld\r", tmp);
            strcat(info, info2);
            serial.printf("%s", info);
            tp.printf("%s", info);

            tmp = buf[12 + 7] & 0x1F;
            tmp = (tmp << 8) + buf[12 + 8];
            tmp = (tmp << 5) + ((buf[12 + 9] & 0xF8) >> 3);
            if (tmp != 0) {
                snprintf(info, sizeof(info), "利用額: %ld円", tmp);
                serial.printf("%s\n", info);
                tp.printf("%s\r", info);
            }

            tmp = buf[12 + 9] & 0x07;
            tmp = (tmp << 8) + buf[12 + 10];
            tmp = (tmp << 6) + ((buf[12 + 11] & 0xFC) >> 2);
            if (tmp != 0) {
                snprintf(info, sizeof(info), "チャージ額: %ld円", tmp);
                serial.printf("%s\n", info);
                tp.printf("%s\r", info);
            }

            tmp = (buf[12 + 5] & 0x7F);
            tmp = (tmp << 8) + buf[12 + 6];
            tmp = (tmp << 3) + ((buf[12 + 7] & 0xE0) >> 5);
            snprintf(info, sizeof(info), "残高: %ld円", tmp);
            serial.printf("%s\n", info);
            tp.printf("%s\r\r", info);

        }
    }
    if (requestService(WAON_SERVICE_CODE2) && readEncryption(WAON_SERVICE_CODE2, 0, buf)) {
        tmp = buf[12 + 0];
        tmp = (tmp << 8) + buf[12 + 1];
        tmp = (tmp << 8) + buf[12 + 2];
        snprintf(info, sizeof(info), "\r20%x年%x月%x日 ポイント残高 %ldpt\r", buf[12 + 11], buf[12 + 12], buf[12 + 13], tmp);
        serial.printf("%s", info);
        tp.printf("%s", info);
    }
}

void parse_history_edy(uint8_t *buf)
{
    char info[100], info2[30];
    uint32_t tmp;

    tmp = buf[4];
    int day = (((tmp << 8) + buf[5]) >> 1);
    if (day == 0) {
        return;
    }
    int sec = ((buf[5]&1) << 16) + (buf[6] << 8) + buf[7];

    struct tm tm, *pt;
    time_t t = (time_t)(-1);
    tm.tm_year = 2000 - 1900;
    tm.tm_mon = 1 - 1;
    tm.tm_mday = 1 + day;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = sec;
    t = mktime(&tm);
    pt = localtime(&t);
    
    snprintf(info, sizeof(info), "-----\n");
    serial.printf("%s", info);
    tp.printf("\r");

    snprintf(info, sizeof(info), "種別: ");
    switch(buf[0]) {
        case 0x02:
            snprintf(info2, sizeof(info2), "チャージ ");
            break;
        case 0x04:
            snprintf(info2, sizeof(info2), "バリューチャージ ");
            break;
        case 0x20:
            snprintf(info2, sizeof(info2), "支払い ");
            break;
        default:
            snprintf(info2, sizeof(info2), "不明(%d) ", buf[12]);
            break;
    }
    strcat(info, info2);
    serial.printf("%s\n", info);
    tp.printf("%s\r", info);
    
    snprintf(info, sizeof(info), "利用日時: %d年%d月%d日 %02d:%02d", pt->tm_year+1900, pt->tm_mon+1, pt->tm_mday, pt->tm_hour, pt->tm_min);
    serial.printf("%s\n", info);
    tp.printf("%s\r", info);

    tmp = buf[8];
    tmp = (tmp << 8) + buf[9];
    tmp = (tmp << 8) + buf[10];
    tmp = (tmp << 8) + buf[11];
    snprintf(info, sizeof(info), "利用額: %ld円", tmp);
    serial.printf("%s\n", info);
    tp.printf("%s\r", info);
    
    tmp = buf[12];
    tmp = (tmp << 8) + buf[13];
    tmp = (tmp << 8) + buf[14];
    tmp = (tmp << 8) + buf[15];
    snprintf(info, sizeof(info), "残高: %ld円", tmp);
    serial.printf("%s\n", info);
    tp.printf("%s\r\r", info);
}

int requestService(uint16_t serviceCode){
    int ret;
    uint8_t buf[RCS620S_MAX_CARD_RESPONSE_LEN];
    uint8_t responseLen = 0;
    
    buf[0] = 0x02;
    memcpy(buf + 1, rcs620s.idm, 8);
    buf[9] = 0x01;
    buf[10] = (uint8_t)((serviceCode >> 0) & 0xff);
    buf[11] = (uint8_t)((serviceCode >> 8) & 0xff);
    
    ret = rcs620s.cardCommand(buf, 12, buf, &responseLen);
    
    if (!ret || (responseLen != 12) || (buf[0] != 0x03) ||
        (memcmp(buf + 1, rcs620s.idm, 8) != 0) || ((buf[10] == 0xff) && (buf[11] == 0xff))) {
        return 0;
    }
    
    return 1;
}

int readEncryption(uint16_t serviceCode, uint8_t blockNumber, uint8_t *buf){
    int ret;
    uint8_t responseLen = 0;
    
    buf[0] = 0x06;
    memcpy(buf + 1, rcs620s.idm, 8);
    buf[9] = 0x01; // サービス数
    buf[10] = (uint8_t)((serviceCode >> 0) & 0xff);
    buf[11] = (uint8_t)((serviceCode >> 8) & 0xff);
    buf[12] = 0x01; // ブロック数
    buf[13] = 0x80;
    buf[14] = blockNumber;
    
    ret = rcs620s.cardCommand(buf, 15, buf, &responseLen);
    
    if (!ret || (responseLen != 28) || (buf[0] != 0x07) ||
        (memcmp(buf + 1, rcs620s.idm, 8) != 0)) {
        return 0;
    }

    return 1;
}

void printBalanceLCD(const char *card_name, uint32_t balance)
{
    lcd.clear();
    lcd.printf(0, 0, (char*)"%s", card_name);
    lcd.printf(0, 1, (char*)"\\ %d", balance);
}

void get_station_name(char *buf, int area, int line, int station) {
    unsigned int offset = 0;
    while (1) {
        if (sc_utf8[0 + offset] == area &&
            sc_utf8[1 + offset] == line &&
            sc_utf8[2 + offset] == station) {
            snprintf(buf, 80, "%s線 %s駅", &sc_utf8[3 + offset], &sc_utf8[3 + 40 + offset]);
            break;
        }
        else {
            offset += record_length;
        }

        if (offset >= sc_utf8_len) {
            snprintf(buf, 80, "***");
            break;
        }
    }
}
