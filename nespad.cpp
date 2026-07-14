#include "hardware/pio.h"

#define nespad_wrap_target 0

// Both program variants clock 16 bits so SNES controllers (which share the
// NES latch/clock/data protocol but shift out 16 bits: B,Y,Select,Start,
// Up,Down,Left,Right,A,X,L,R + 4 ID bits) are read in full. Plain NES
// controllers simply shift out low for bits 9-16; nespad_read_finish()
// detects that and masks the result back to 8 bits.
#if HW_CONFIG == 12 || HW_CONFIG == 13 // Murmulator M1/M2 bothe controllers ahare latch and clock
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    // @javavi pull request
    // from https://github.com/fhoedemakers/pico-infonesPlus/pull/167
    0xD020, //  0: irq    wait 0          side 1
    0xFA01, //  1: set    pins, 1         side 1 [10]
    0xF02F, //  2: set    x, 15           side 1
    0xE000, //  3: set    pins, 0         side 0
    0x4401, //  4: in     pins, 1         side 0 [4]
    0xF400, //  5: set    pins, 0         side 1 [4]
    0x1043, //  6: jmp    x--, 3          side 1
    //     .wrap
  };
#else // Original nespad code, used as long as the above code is not tested on WaveShare boards.
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    0xd020, //  0: irq    wait 0          side 1
    0xf101, //  1: set    pins, 1         side 1 [1]
    0xf02f, //  2: set    x, 15           side 1
    0xf000, //  3: set    pins, 0         side 1
    0xf400, //  4: set    pins, 0         side 1 [4]
    0xe100, //  5: set    pins, 0         side 0 [1]
    0x5301, //  6: in     pins, 1         side 1 [3]
    0x1044, //  7: jmp    x--, 4          side 1
            //     .wrap
};
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
// Full 16-clock read in SNES serial order: bit0=B, 1=Y, 2=Select, 3=Start,
// 4=Up, 5=Down, 6=Left, 7=Right, 8=A, 9=X, 10=L, 11=R (1 = pressed).
// A plain NES controller only populates bits 0-7 (A,B,Select,Start,dpad) —
// for those pads this is identical to nespad_states[].
uint16_t nespad_states_ext[2] = {0, 0};
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
  sm_config_set_in_shift(&c, true, true, 16);
  sm_config_set_clkdiv_int_frac(&c, cpu_khz / 1000, 0);

  pio_set_irq0_source_enabled(pio[padnum], (pio_interrupt_source)(pis_interrupt0 + sm[padnum]), false);
  pio_sm_clear_fifos(pio[padnum], sm[padnum]);

  pio_sm_init(pio[padnum], sm[padnum], offset, &c);
  pio_sm_set_enabled(pio[padnum], sm[padnum], true);
  return true;
}

// Initiate nespad read. Non-blocking; result will be available in ~200 uS
// via nespad_read_finish(). Must first call nespad_begin() once to set up PIO.
void nespad_read_start(void)
{
  pio_interrupt_clear(pio[0], 0);
  if (second_pad)
  {
    pio_interrupt_clear(pio[1], 0);
  }
}

// Finish nespad read. Ideally should be called ~200 uS after
// nespad_read_start(), but can be sooner (will block until ready), or later
// (will introduce latency). Sets nespad_states[] (legacy 8-bit view, 1 =
// pressed: 0x01=A/B, 0x02=B/Y, 0x04=Select, 0x08=Start, 0x10=Up, 0x20=Down,
// 0x40=Left, 0x80=Right — first name NES pad, second SNES pad) and
// nespad_states_ext[] (full SNES-order 12-button word, see declaration).
// Must first call nespad_begin() once to set up PIO. Result will be 0 if
// PIO failed to init (e.g. no free state machine).
static uint16_t nespad_decode(int padnum)
{
  if (sm[padnum] < 0)
    return 0;
  // Right-shift was used in sm config so bit order matches NES controller
  // bits used elsewhere in picones, but does require shifting down...
  uint32_t raw = (pio_sm_get_blocking(pio[padnum], sm[padnum]) >> 16) ^ 0xFFFF;
  // NES controller: the 4021's serial input is grounded, so clocks 9-16
  // read low on the wire = 1 after inversion. A SNES controller drives the
  // 4 ID bits (13-16) high = 0 after inversion, so they can never all be
  // set. Disconnected port reads high via pull-up = all zeros.
  if ((raw & 0xF000) == 0xF000)
    raw &= 0x00FF; // NES pad: keep the 8 real buttons
  return (uint16_t)(raw & 0x0FFF); // SNES pad: strip the ID bits
}

void nespad_read_finish(void)
{
  nespad_states_ext[0] = nespad_decode(0);
  nespad_states_ext[1] = second_pad ? nespad_decode(1) : 0;
  nespad_states[0] = (uint8_t)nespad_states_ext[0];
  nespad_states[1] = (uint8_t)nespad_states_ext[1];
}
