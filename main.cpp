/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "USBSerial.h"
#include "SB1602E.h"
#include "RCS620S.h"

// Blinking rate in milliseconds
#define BLINKING_RATE     500ms


// RCS620S
#define PUSH_TIMEOUT                  2100
#define COMMAND_TIMEOUT               400
#define POLLING_INTERVAL              500ms
#define RCS620S_MAX_CARD_RESPONSE_LEN 30
 
// FeliCa Service/System Code
#define CYBERNE_SYSTEM_CODE           0x0003
#define COMMON_SYSTEM_CODE            0xFE00
#define PASSNET_SERVICE_CODE          0x090F
#define EDY_SERVICE_CODE              0x170F
#define NANACO_SERVICE_CODE           0x564F
#define WAON_SERVICE_CODE             0x680B

#define PRINT_ENTRIES                 20    // Max. 20

int requestService(uint16_t serviceCode);
int readEncryption(uint16_t serviceCode, uint8_t blockNumber, uint8_t *buf);
void printBalanceLCD(const char *card_name, uint32_t balance);

DigitalOut led(LED1);
USBSerial serial(true);
SB1602E lcd(p14, p15);
RCS620S rcs620s(p16, p17);

void parse_history(uint8_t *buf)
{
    char info[80], info2[40];
    int region_in, region_out, line_in, line_out, station_in, station_out;

    region_in = (buf[0xf] >> 6) & 3;
    region_out = (buf[0xf] >> 4) & 3;
    line_in = buf[6];
    line_out = buf[7];
    station_in = buf[8];
    station_out = buf[9];

    for(int i = 0; i < 16; i++) {
        serial.printf("%02X ", buf[i]);
    }
    serial.printf("\n");

    sprintf(info, "機種種別: ");
    switch (buf[0]) {
        case 0x03:
            strcat(info, "のりこし精算機\r");
            break;
        case 0x05:
            strcat(info, "バス/路面等\r");
            break;
        case 0x07:
        case 0x08:
        case 0x12:
            strcat(info, "自動券売機\r");
            break;
        case 0x14:
            strcat(info, "駅窓口\r");
            break;
        case 0x15:
            strcat(info, "定期券発売機\r");
            break;            
        case 0x16:
            strcat(info, "自動改札機\r");
            break;
        case 0x17:
            strcat(info, "簡易改札機\r");
            break;
        case 0x18:
            strcat(info, "駅務機器\r");
            break;
        case 0x46:
            strcat(info, "ビューアルッテ端末\r");
            break;
        case 0xc7:
        case 0xc8:
            strcat(info, "物販端末\r");
            break;
        default:
            strcat(info, "不明\r");
            break;
    }
    serial.printf("%s", info);
    // tp.printf("%s", info);

    sprintf(info, "利用種別: ");
    switch (buf[1]) {
        case 0x01:
            strcat(info, "自動改札出場\r");
            break;
        case 0x02:
            strcat(info, "SFチャージ\r");
            break;
        case 0x03:
            strcat(info, "きっぷ購入\r");
            break;
        case 0x04:
            strcat(info, "磁気券精算\r");
            break;
        case 0x05:
            strcat(info, "乗越精算\r");
            break;
        case 0x06:
            strcat(info, "窓口精算\r");
            break;
        case 0x07:
            strcat(info, "新規\r");
            break;
        case 0x08:
            strcat(info, "チャージ控除\r");
            break;
        case 0x0C:
        case 0x0D:
        case 0x0F:
            strcat(info, "バス/路面等\r");
            break;
        case 0x14:
            strcat(info, "オートチャージ\r");
            break;
        case 0x46:
            strcat(info, "物販\r");
            break;
        default:
            strcat(info, "不明\r");
            break;
    }
    serial.printf("%s", info);
    // tp.printf("%s", info);

    if (buf[2] != 0) {
        sprintf(info, "支払種別: ");
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
        // tp.printf("%s", info);
    }

    if (buf[1] == 0x01 || buf[1] == 0x14) {
        sprintf(info, "入出場種別: ");
        switch (buf[3]) {
            case 0x01:
                strcat(info, "入場");
                sprintf(info2, " %02X %02X", buf[6], buf[7]);
                strcat(info, info2);
                break;
            case 0x02:
                strcat(info, "出場");
                sprintf(info2, " %02X %02X %02X %02X", buf[6], buf[7], buf[8], buf[9]);
                strcat(info, info2);
                break;
            case 0x03:
                strcat(info, "定期入場");
                sprintf(info2, " %02X %02X", buf[6], buf[7]);
                strcat(info, info2);
                break;
            case 0x04:
                strcat(info, "定期出場");
                sprintf(info2, " %02X %02X %02X %02X", buf[6], buf[7], buf[8], buf[9]);
                strcat(info, info2);
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
                strcat(info, "乗継割引");
                break;
            case 0x21:
                strcat(info, "バス等乗継割引");
                break;
            case 0x22:
            case 0x25:
            case 0x26:
                strcat(info, "券面外乗降");
                sprintf(info2, " [%d] %02X %02X [%d] %02X %02X", (buf[0xf] >> 6) & 3, buf[6], buf[7], (buf[0xf] >> 4) & 3, buf[8], buf[9]);
                strcat(info, info2);
                break;
            default:
                strcat(info, "不明");
                sprintf(info2, " %02X %02X %02X %02X", buf[6], buf[7], buf[8], buf[9]);
                strcat(info, info2);
                break;
        }
        serial.printf("%s\r", info);
        // tp.printf("%s\r", info);
    }

    sprintf(info, "処理日付: %d/%02d/%02d", 2000+(buf[4]>>1), ((buf[4]&1)<<3 | ((buf[5]&0xe0)>>5)), buf[5]&0x1f);
    if (buf[1] == 0x46) {   // 物販
        sprintf(info2, " %02d:%02d:%02d", (buf[6] & 0xF8) >> 3, ((buf[6] & 0x7) >> 5) | ((buf[7] & 0xe0) >> 5), (buf[7] & 0x1f));
        strcat(info, info2);
    }
    strcat(info, "\r");
    serial.printf("%s", info);
    // tp.printf("%s", info);

    sprintf(info, "残額: %d円\r\r", (buf[11]<<8) + buf[10]); 
    serial.printf("%s\n", info);
    // tp.setDoubleSizeWidth();
    // tp.printf("%s", info);
    // tp.clearDoubleSizeWidth();
}

#if 0
int main()
{
    // Initialise the digital pin LED1 as an output
    DigitalOut led(LED1);
    int cnt = 0;

    lcd.setCharsInLine(8);
    lcd.clear();
    lcd.contrast(0x35);
    lcd.printf(0, 0, (char*)"FeliCa");
    lcd.printf(0, 1, (char*)"Reader");

    rcs620s.initDevice();

    serial.init();
    serial.connect();
    serial.printf("Hello, Raspberry Pi pico!\n");
    serial.printf("Build: %s %s\n\n", __DATE__, __TIME__);

    while (true) {
        serial.printf("cnt = %d\n", cnt++);
        led = !led;
        ThisThread::sleep_for(BLINKING_RATE);
    }
}
#endif

int main()
{
    uint8_t buffer[20][16];
    uint8_t idm[8];
    
    lcd.setCharsInLine(8);
    lcd.clear();
    lcd.contrast(0x35);
    lcd.printf(0, 0, (char*)"FeliCa");
    lcd.printf(0, 1, (char*)"Reader");

    ThisThread::sleep_for(500ms);
    serial.printf("\n*** RCS620S テストプログラム ***\n\n");

    rcs620s.initDevice();
    // tp.initialize();
    // tp.putLineFeed(1);
    memset(idm, 0, 8);

    while(1) {
        uint32_t balance;
        uint8_t buf[RCS620S_MAX_CARD_RESPONSE_LEN];
        int isCaptured = 0;
        
        rcs620s.timeout = COMMAND_TIMEOUT;
        
        // サイバネ領域
        if(rcs620s.polling(CYBERNE_SYSTEM_CODE)){
            // Suica or PASMO
            if(requestService(PASSNET_SERVICE_CODE)){
                for (int i = 0; i < 20; i++) {
                    if(readEncryption(PASSNET_SERVICE_CODE, i, buf)){
                        memcpy(buffer[i], &buf[12], 16);
                    }
                }
                if (memcmp(idm, buf+1, 8) != 0) {
                    // カード変更
                    isCaptured = 1;
                    memcpy(idm, buf+1, 8);
                }
                else {
                    // 前と同じカード
                    isCaptured = 0;
                }
            }
        }
        
        // 共通領域
        else if(rcs620s.polling(COMMON_SYSTEM_CODE)){
            // Edy
            if(requestService(EDY_SERVICE_CODE)){
            if(readEncryption(EDY_SERVICE_CODE, 0, buf)){
                // Big Endianで入っているEdyの残高を取り出す
                balance = buf[26];                  // 14 byte目
                balance = (balance << 8) + buf[27]; // 15 byte目
                // 残高表示
                printBalanceLCD("Edy", balance);
            }
            }
            
            // nanaco
            if(requestService(NANACO_SERVICE_CODE)){
            if(readEncryption(NANACO_SERVICE_CODE, 0, buf)){
                // Big Endianで入っているNanacoの残高を取り出す
                balance = buf[17];                  // 5 byte目
                balance = (balance << 8) + buf[18]; // 6 byte目
                balance = (balance << 8) + buf[19]; // 7 byte目
                balance = (balance << 8) + buf[20]; // 8 byte目
                // 残高表示
                printBalanceLCD("nanaco", balance);
            }
            }
            
            // waon
            if(requestService(WAON_SERVICE_CODE)){
            if(readEncryption(WAON_SERVICE_CODE, 1, buf)){
                // Big Endianで入っているWaonの残高を取り出す
                balance = buf[17];                  // 21 byte目
                balance = (balance << 8) + buf[18]; // 22 byte目
                balance = (balance << 8) + buf[19]; // 23 byte目
                balance = balance & 0x7FFFE0;       // 残高18bit分のみ論理積で取り出す
                balance = balance >> 5;             // 5bit分ビットシフト
                // 残高表示
                printBalanceLCD("waon", balance);
            }
            }
        }
                
        if (isCaptured) {
            // 残高表示 (Suica or PASMO)
            balance = buffer[0][11];                  // 11 byte目
            balance = (balance << 8) + buffer[0][10]; // 10 byte目
            lcd.clear();
            lcd.printf(0, 0, (char*)"Suica");
            lcd.printf(0, 1, (char*)"\\ %d", balance);
            for (int i = 0; i < PRINT_ENTRIES; i++) {
                if (buffer[i][0] != 0) {
                    parse_history(&buffer[i][0]);
                }
            }
            // tp.putLineFeed(4);
            isCaptured = 0;
        }

        rcs620s.rfOff();
        led = !led;
        ThisThread::sleep_for(POLLING_INTERVAL);
    }
}


// request service
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
    
    if(!ret || (responseLen != 12) || (buf[0] != 0x03) ||
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
    printf("%s: %ld\n", card_name, balance);
    lcd.clear();
    lcd.printf(0, 0, (char*)"%s", card_name);
    lcd.printf(0, 1, (char*)"\\ %d", balance);
}
