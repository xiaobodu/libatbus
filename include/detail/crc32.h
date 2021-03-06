﻿#pragma once

#ifndef LIBATBUS_DETAIL_CRC32_H_
#define LIBATBUS_DETAIL_CRC32_H_

#include <stdint.h>
#include <stddef.h>

namespace atbus {
    namespace detail {
        uint32_t crc32(uint32_t crc, const unsigned char* s, size_t l);
    }
}

#endif
