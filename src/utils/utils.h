// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#pragma once

#include "macros.h"

namespace fasto {
namespace utils {

unsigned char * hex_encode(const void* bytes, uint16_t size);  // allocate char*,
                                                               // deleting the storage
                                                               // when it is no longer needed

int Base64encode_len(int len);
int Base64encode(char * coded_dst, const char *plain_src, int len_plain_src);

int Base64decode_len(const char * coded_src);
int Base64decode(char * plain_dst, const char *coded_src);

}  // namespace utils
}  // namespace fasto
