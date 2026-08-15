The file **pico_nesPCB_v2.6.zip** in this folder is the current PCB release, as found on the [releases](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest) page. This zip file contains the Gerber files and can be uploaded to a PCB manufacturer of choice.

**pico_nesPCB_v2.6.zip** includes through-holes, allowing a Raspberry Pi Pico, Pico 2, or [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) **with pin headers** installed to be used. It also corrects the silkscreen labelling of the D3/D4 pads, see the note below.

**pico_nesPCB_v2.1.zip** is identical to v2.0, except that D3 and D4 of NES controller port 2 are mapped to **GPIO27 (D3)** and **GPIO28 (D4)**. This is used by the NES Zapper support, which is currently in beta.

> [!NOTE]
> The **v2.1 silkscreen labels these two pads the wrong way round**: what is printed as D3 is the physical D4 line, and what is printed as D4 is the physical D3 line. Only the printing is wrong - the routing is as stated above, and a controller port soldered into the footprint works correctly on both revisions. **v2.6 corrects the labels.** No firmware change is needed for either revision.

**Gerber_PicoNES_Mini_PCB_v2.0.zip** is for the PicoNES Mini with a Waveshare RP2040/RP2350 Zero board.

**Gerber_PicoNES_Micro_v1.2.zip** is for the PicoNES Micro with a Waveshare RP2350 USB A board.

The v0.2 folder contains the previous version of the design.
