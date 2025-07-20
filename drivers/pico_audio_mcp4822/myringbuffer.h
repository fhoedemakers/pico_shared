#ifndef MYRINGBUFFER_H
#define MYRINGBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <hardware/sync.h>


#ifdef __cplusplus
extern "C" {
#endif

#define MYRINGBUFFER_SIZE 1024  // Must be power of 2
#define MYRINGBUFFER_MASK (MYRINGBUFFER_SIZE - 1)

void     my_rb_init(void);
bool     my_rb_put(uint16_t sample);
bool     my_rb_get(uint16_t *sample);
uint32_t my_rb_free(void);
uint32_t my_rb_used(void);

#ifdef __cplusplus
}
#endif

#endif  // MYRINGBUFFER_H
