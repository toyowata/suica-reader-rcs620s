/* AS289R2 thermal control component for mbed OS
 * Copyright (c) 2016-2024, Toyomasa Watarai
 * SPDX-License-Identifier: Apache-2.0
*/

#ifndef MBED_AS289R2_STUB_H
#define MBED_AS289R2_STUB_H

#include "AS289R2.h"

class AS289R2_STUB : public AS289R2
{
public:
    using AS289R2 ::AS289R2;
    void initialize(void) {};
    void putLineFeed(uint32_t lines) {};
    void clearBuffer(void) {};
    void setDoubleSizeWidth(void) {};
    void clearDoubleSizeWidth(void) {};
    int printf(const char *format, ...) {return 0;};
};

#endif
