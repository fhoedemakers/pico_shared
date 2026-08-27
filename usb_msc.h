#pragma once

// USB drive mode: expose the SD card to a PC as a USB mass-storage device.
//
// Only built when FRENS_USB_MSC is 1, which the top-level CMakeLists sets for
// RP2350 builds. The whole feature is reachable from one place: the "USB drive
// mode" entry in the settings menu, which is offered only when the settings
// menu was opened from the rom browser (never in-game).
//
// The device stack is started by usbMscBegin() and stopped again by usbMscEnd(),
// so outside that window there is no device IRQ installed and tud_task() is
// never called. That is what keeps the feature free at runtime: the emulator
// loop and Menu_LoadFrame() are untouched.
//
// While the device stack is up, FatFs is unmounted and MUST NOT be used: the
// SCSI callbacks drive disk_read()/disk_write() directly and the PC owns the
// filesystem. usbMscBegin()/usbMscEnd() are what enforce that.

#ifndef FRENS_USB_MSC
#define FRENS_USB_MSC 0
#endif

#if FRENS_USB_MSC

namespace Frens
{
    // Unmount FatFs, hand the native USB port over to the device stack and
    // start advertising the SD card. On boards where the USB host owns the
    // native port (no PIO USB) this also tears the host stack down, so USB
    // gamepads stop working until usbMscEnd(). Returns false if the SD card
    // could not be sized, in which case the caller must not enter the screen.
    bool usbMscBegin();

    // Stop the device stack, restore the USB host if it was torn down and
    // remount FatFs at the directory the browser was in.
    void usbMscEnd();

    // Pump the device stack. Call as often as possible while in USB drive mode.
    void usbMscTask();

    // True while a PC has the volume mounted. Goes false on eject or unplug,
    // which is how the screen knows it may return. A bus suspend does not
    // count - see usbMscHostSuspended().
    bool usbMscHostConnected();

    // True while the host has the bus suspended. Display only: a suspend is
    // not a reason to leave USB drive mode.
    bool usbMscHostSuspended();

    // True once a host has enumerated us at least once since usbMscBegin().
    // Used to distinguish "no PC attached" from "PC ejected the drive".
    bool usbMscEverConnected();

    // True on boards where leaving USB drive mode cannot be done cleanly and
    // the caller must reboot instead: without PIO USB the USB host has to take
    // rhport 0 back, which forces a tud_deinit() that leaks two hardware
    // spinlocks TinyUSB never frees. Rebooting keeps a second visit from
    // running the board out of them.
    bool usbMscNeedsRebootOnExit();

    // Read-and-clear: true if the host wrote any sector since the last call.
    // Drives the rom list refresh on the way out.
    bool usbMscMediaDirty();
}

#endif // FRENS_USB_MSC
