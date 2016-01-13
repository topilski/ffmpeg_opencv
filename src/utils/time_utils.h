// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#pragma once

#include <time.h>

#include "macros.h"

namespace fasto {
namespace utils {

char* convert_ms_2string(uint64_t mssec);  // deleting the storage when it is no longer needed
uint64_t currentms();
uint64_t systemms();
uint64_t currentns();
uint64_t convert_timespec_2ms(struct timespec tp);
char * format_timestamp(uint64_t timestamp);  // deleting the storage when it is no longer needed

}  // namespace utils
}  // namespace fasto
