#include "hardware/pio.h"

#define nespad_wrap_target 0
#ifndef USE_OLD_PIO_NESPAD_PROGRAM
#define USE_OLD_PIO_NESPAD_PROGRAM 0
#endif
#if !USE_OLD_PIO_NESPAD_PROGRAM
  static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    // from https://github.com/fhoedemakers/pico-infonesPlus/pull/167
    0xD020, //  0: irq    wait 0          side 1
    0xFA01, //  1: set    pins, 1         side 1 [10]
    0xF027, //  2: set    x, 7            side 1
    0xE000, //  3: set    pins, 0         side 0
    0x4401, //  4: in     pins, 1         side 0 [4]
    0xF400, //  5: set    pins, 0         side 1 [4]
    0x1043, //  6: jmp    x--, 3          side 1
    //     .wrap
  };
#else
#if HW_CONFIG == 12 || HW_CONFIG == 13 // Murmulator M1/M2 will need the older version of the PIO program.
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    0xc020, //  0: irq    wait 0          side 0
    0xea01, //  1: set    pins, 1         side 0 [10]
    0xe027, //  2: set    x, 7            side 0
    0xe000, //  3: set    pins, 0         side 0
    0x4401, //  4: in     pins, 1         side 0 [4]
    0xf500, //  5: set    pins, 0         side 1 [5]
    0x0044, //  6: jmp    x--, 4          side 0
            //     .wrap
};
#else // Most recent version of the NES pad PIO program, needed for all the other boards
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    0xd020, //  0: irq    wait 0          side 1
    0xf101, //  1: set    pins, 1         side 1 [1]
    0xf027, //  2: set    x, 7            side 1
    0xf000, //  3: set    pins, 0         side 1
    0xf400, //  4: set    pins, 0         side 1 [4]
    0xe100, //  5: set    pins, 0         side 0 [1]
    0x5301, //  6: in     pins, 1         side 1 [3]
    0x1044, //  7: jmp    x--, 4          side 1
            //     .wrap
};
#endif
#endif
// Derive program length from instruction array (works for both variants)
#define NESPAD_PROG_LEN (sizeof(nespad_program_instructions) / sizeof(nespad_program_instructions[0]))
static const struct pio_program nespad_program = {
    .instructions = nespad_program_instructions,
    .length = NESPAD_PROG_LEN,
    .origin = -1,
};

static inline pio_sm_config nespad_program_get_default_config(uint offset)
{
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset + nespad_wrap_target, offset + (NESPAD_PROG_LEN - 1));
  sm_config_set_sideset(&c, 1, false, false);
  return c;
}

#if defined(PICO_RP2350)
#define NES_NUM_PIOS 3
#elif defined(PICO_RP2040)
#define NES_NUM_PIOS 2
#else
#define NES_NUM_PIOS 2
#endif

static PIO pio[2];
static int8_t sm[2] = {-1, -1};
static bool second_pad = false;

// Cache program once per PIO instance
static int program_offset[NES_NUM_PIOS];      // valid only if program_added[i] is true
static bool program_added[NES_NUM_PIOS] = {}; // default-initialized to false

uint8_t nespad_states[2] = {0, 0};
uint8_t nespad_state = 0;
bool nespad_begin(uint8_t padnum, uint32_t cpu_khz, uint8_t clkPin, uint8_t dataPin,
                  uint8_t latPin, PIO _pio)
{
  if (padnum > 1)
    return false;
  if (padnum == 1)
    second_pad = true;

  pio[padnum] = _pio;
  int pio_idx = pio_get_index(_pio);

  // Claim an SM first
  if ((sm[padnum] = pio_claim_unused_sm(pio[padnum], true)) < 0)
    return false;

  // Load program once per PIO instance
  if (!program_added[pio_idx])
  {
    if (!pio_can_add_program(pio[padnum], &nespad_program))
      return false;
    program_offset[pio_idx] = pio_add_program(pio[padnum], &nespad_program);
    program_added[pio_idx] = true;
  }
  uint offset = (uint)program_offset[pio_idx];

  pio_sm_config c = nespad_program_get_default_config(offset);
  sm_config_set_sideset_pins(&c, clkPin);
  sm_config_set_in_pins(&c, dataPin);
  sm_config_set_set_pins(&c, latPin, 1);
  pio_gpio_init(pio[padnum], clkPin);
  pio_gpio_init(pio[padnum], dataPin);
  pio_gpio_init(pio[padnum], latPin);
  gpio_set_pulls(dataPin, true, false);
  pio_sm_set_pindirs_with_mask(pio[padnum], sm[padnum],
                               (1u << clkPin) | (1u << latPin),
                               (1u << clkPin) | (1u << dataPin) | (1u << latPin));
  sm_config_set_in_shift(&c, true, true, 8);
  sm_config_set_clkdiv_int_frac(&c, cpu_khz / 1000, 0);

  pio_set_irq0_source_enabled(pio[padnum], (pio_interrupt_source)(pis_interrupt0 + sm[padnum]), false);
  pio_sm_clear_fifos(pio[padnum], sm[padnum]);

  pio_sm_init(pio[padnum], sm[padnum], offset, &c);
  pio_sm_set_enabled(pio[padnum], sm[padnum], true);
  return true;
}

// Initiate nespad read. Non-blocking; result will be available in ~100 uS
// via nespad_read_finish(). Must first call nespad_begin() once to set up PIO.
void nespad_read_start(void)
{
  pio_interrupt_clear(pio[0], 0);
  if (second_pad)
  {
    pio_interrupt_clear(pio[1], 0);
  }
}

// Finish nespad read. Ideally should be called ~100 uS after
// nespad_read_start(), but can be sooner (will block until ready), or later
// (will introduce latency). Sets value of global nespad_state variable, a
// bitmask of button/D-pad state (1 = pressed). 0x01=Right, 0x02=Left,
// 0x04=Down, 0x08=Up, 0x10=Start, 0x20=Select, 0x40=B, 0x80=A. Must first
// call nespad_begin() once to set up PIO. Result will be 0 if PIO failed to
// init (e.g. no free state machine).
void nespad_read_finish(void)
{
  // Right-shift was used in sm config so bit order matches NES controller
  // bits used elsewhere in picones, but does require shifting down...
  nespad_states[0] = (sm[0] >= 0) ? ((pio_sm_get_blocking(pio[0], sm[0]) >> 24) ^ 0xFF) : 0;
  if (second_pad)
  {
    nespad_states[1] = (sm[1] >= 0) ? ((pio_sm_get_blocking(pio[1], sm[1]) >> 24) ^ 0xFF) : 0;
  }
  else
  {
    nespad_states[1] = 0;
  }
}
