#include "myringbuffer.h"
#include "hardware/irq.h"  // Optional: for disabling/enabling IRQs safely
#include "stdio.h"     // Optional: for printf debugging
static volatile uint16_t buffer[MYRINGBUFFER_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

void my_rb_init(void) {
    head = 0;
    tail = 0;
}

bool __not_in_flash_func(my_rb_put)(uint16_t sample) {
    uint32_t next = (head + 1) & MYRINGBUFFER_MASK;
    if (next == tail) {
        printf("Buffer full\n");
        return false;  // Buffer full
    }
    buffer[head] = sample;
    head = next;
    return true;
}

bool __not_in_flash_func(my_rb_get)(uint16_t *sample) {
    if (head == tail) return false;  // Buffer empty
    *sample = buffer[tail];
    tail = (tail + 1) & MYRINGBUFFER_MASK;
    return true;
}

uint32_t __not_in_flash_func(my_rb_free(void)) {
    //  uint32_t irq = save_and_disable_interrupts();
    // __mem_fence_acquire();
    uint32_t h = head;
    uint32_t t = tail;
    //  __mem_fence_acquire();  
    // restore_interrupts(irq);
    return (t - h - 1) & MYRINGBUFFER_MASK;
}

uint32_t my_rb_used(void) {
    uint32_t h = head;
    uint32_t t = tail;
    return (h - t) & MYRINGBUFFER_MASK;
}
