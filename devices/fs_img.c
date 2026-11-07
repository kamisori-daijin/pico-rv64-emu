#include <stdint.h>

#if __has_include("../fs_img.inc")
#include "../fs_img.inc"
#else
const unsigned char fs_img[] = {0};
const unsigned int fs_img_len = 0;
#endif