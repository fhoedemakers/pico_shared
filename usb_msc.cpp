#include "usb_msc.h"

#if FRENS_USB_MSC

#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/dcd.h"
#include "ff.h"
#include "diskio.h"
#include "FrensHelpers.h"

// The SCSI callbacks below talk to the card through the FatFs block layer in
// drivers/pico_fatfs/tf_card.c, not through FatFs itself: the PC owns the
// filesystem while we are mounted, so going through f_read/f_write would only
// give two writers on the same FAT. Drive 0 is the only drive tf_card.c knows.
#define MSC_DRIVE 0
#define MSC_BLOCK_SIZE 512

namespace
{
    volatile bool hostMounted = false;   // set/cleared from the USB IRQ callbacks
    volatile bool hostSuspended = false; // bus suspended; not a reason to leave
    volatile bool mediaDirty = false;    // host wrote at least one sector
    volatile bool everConnected = false;
    volatile bool ejected = false;       // host issued START_STOP with eject
    // The device stack is initialised at most once per boot and then left
    // that way; only the bus attachment is toggled. tud_deinit() cannot be
    // used repeatedly: dcd_init() -> rp2040_usb.c critical_section_init(
    // &rp2usb_lock) and usbd.c osal_spin_init(&_usbd_spin) each claim a
    // hardware spinlock, and TinyUSB frees neither on deinit (there is no
    // osal_spin_deinit at all). spin_lock_claim_unused() only hands out ids
    // 24..31, so two leaked per visit panics with "No spinlocks are
    // available" on the third trip into USB drive mode.
    bool deviceInited = false;           // tusb_init(device) done once
    bool deviceActive = false;           // attached to the bus right now
    bool hostTornDown = false;           // non-PIO: tuh_deinit() done
    uint32_t blockCount = 0;
}

namespace Frens
{
    bool usbMscBegin()
    {
        if (deviceActive)
        {
            return true;
        }

        // Size the card before touching anything else: if this fails there is
        // nothing to advertise and the caller must stay out of the screen.
        DWORD sectors = 0;
        if (disk_ioctl(MSC_DRIVE, GET_SECTOR_COUNT, &sectors) != RES_OK || sectors == 0)
        {
            printf("usbMscBegin: cannot read sector count\n");
            return false;
        }
        blockCount = (uint32_t)sectors;

        hostMounted = false;
        hostSuspended = false;
        mediaDirty = false;
        everConnected = false;
        ejected = false;

        // Hand PLL_USB back to the USB hardware. On HSTX builds it is the
        // 126 MHz TMDS source, and with it at 126 MHz the device controller
        // never enumerates - Windows reports "USB device not recognized".
        if (!Frens::usbDeviceClockAcquire())
        {
            printf("usbMscBegin: no 48 MHz USB clock available\n");
            return false;
        }

        // Drop every cached FAT sector before the host can write behind our back.
        Frens::unmountSDCard();

#if !CFG_TUH_RPI_PIO_USB
        // The USB host owns the native port on these boards, so it has to let
        // go before the device stack can have it. USB gamepads go away here;
        // tuh_deinit() walks the device tree and fires tuh_hid_umount_cb(), so
        // the player slots in hid_app.cpp clean themselves up.
        tuh_deinit(BOARD_TUH_RHPORT);
        hostTornDown = true;
#endif

        if (!deviceInited)
        {
            tusb_rhport_init_t dev_init = {
                .role = TUSB_ROLE_DEVICE,
                .speed = TUSB_SPEED_AUTO,
            };
            if (!tusb_init(BOARD_TUD_RHPORT, &dev_init))
            {
                // Put back everything taken above, or the browser would come
                // back with no filesystem and no controllers.
                printf("usbMscBegin: device stack did not start\n");
                if (hostTornDown)
                {
                    tusb_rhport_init_t host_init = {
                        .role = TUSB_ROLE_HOST,
                        .speed = TUSB_SPEED_AUTO,
                    };
                    tusb_init(BOARD_TUH_RHPORT, &host_init);
                    hostTornDown = false;
                }
                Frens::remountSDCard();
                Frens::usbDeviceClockRelease();
                return false;
            }
            deviceInited = true;
        }
        else
        {
            // Already initialised on an earlier visit: just wake the
            // controller and put the pull-up back so the host sees an attach.
            dcd_int_enable(BOARD_TUD_RHPORT);
            tud_connect();
        }
        deviceActive = true;
        printf("USB drive mode: %lu blocks of %d bytes\n",
               (unsigned long)blockCount, MSC_BLOCK_SIZE);
        return true;
    }

    void usbMscEnd()
    {
        if (!deviceActive)
        {
            return;
        }
        // Push anything the card is still holding before we pull the port.
        disk_ioctl(MSC_DRIVE, CTRL_SYNC, 0);

#if CFG_TUH_RPI_PIO_USB
        // Detach from the bus and silence the controller, but leave the stack
        // initialised - see the comment on deviceInited. With the pull-up gone
        // and the interrupt masked the device costs nothing while the emulator
        // runs, and clk_usb going back to 126 MHz cannot disturb it. Nothing
        // else wants this controller, so keeping it initialised is free.
        tud_disconnect();
        dcd_int_disable(BOARD_TUD_RHPORT);
#else
        // Here the USB host has to have rhport 0 back, so the device stack
        // really must be torn down - and that leaks the two spinlocks
        // described on deviceInited. usbMscNeedsRebootOnExit() therefore
        // reports true on these boards and the caller reboots, which is the
        // only way to keep a second visit from panicking.
        tud_deinit(BOARD_TUD_RHPORT);
        deviceInited = false;
#endif
        deviceActive = false;
        hostMounted = false;
        hostSuspended = false;

        if (hostTornDown)
        {
            tusb_rhport_init_t host_init = {
                .role = TUSB_ROLE_HOST,
                .speed = TUSB_SPEED_AUTO,
            };
            tusb_init(BOARD_TUH_RHPORT, &host_init);
            hostTornDown = false;
        }

        Frens::remountSDCard();
        // Last: HSTX goes back onto PLL_USB for the low-jitter TMDS clock.
        Frens::usbDeviceClockRelease();
    }

    void usbMscTask()
    {
        if (deviceActive)
        {
            tud_task();
        }
    }

    bool usbMscHostConnected()
    {
        // Deliberately ignores hostSuspended: a host may suspend the bus
        // briefly (power management) without being finished with the card, and
        // dropping out of USB drive mode mid-copy over that would be worse than
        // waiting. Only an eject or a real disconnect ends the session.
        return hostMounted && !ejected;
    }

    bool usbMscHostSuspended()
    {
        return hostSuspended;
    }

    bool usbMscNeedsRebootOnExit()
    {
#if CFG_TUH_RPI_PIO_USB
        return false; // device stack stays up; nothing leaks
#else
        return true;  // see the tud_deinit() branch in usbMscEnd()
#endif
    }

    bool usbMscEverConnected()
    {
        return everConnected;
    }

    bool usbMscMediaDirty()
    {
        bool dirty = mediaDirty;
        mediaDirty = false;
        return dirty;
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+
extern "C"
{
    void tud_mount_cb(void)
    {
        hostMounted = true;
        everConnected = true;
    }

    void tud_umount_cb(void)
    {
        hostMounted = false;
    }

    void tud_suspend_cb(bool remote_wakeup_en)
    {
        (void)remote_wakeup_en;
        // Note this does not clear hostMounted - see usbMscHostConnected().
        hostSuspended = true;
    }

    void tud_resume_cb(void)
    {
        hostSuspended = false;
    }

    //--------------------------------------------------------------------+
    // SCSI callbacks
    //--------------------------------------------------------------------+

    void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                            uint8_t product_id[16], uint8_t product_rev[4])
    {
        (void)lun;
        // Fixed-width, space padded, not NUL terminated.
        memcpy(vendor_id, "Frens   ", 8);
        memcpy(product_id, "SD Card         ", 16);
        memcpy(product_rev, "1.0 ", 4);
    }

    bool tud_msc_test_unit_ready_cb(uint8_t lun)
    {
        (void)lun;
        if (ejected)
        {
            // Report "medium not present" so the host stops polling us.
            tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
            return false;
        }
        return true;
    }

    void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
    {
        (void)lun;
        *block_count = blockCount;
        *block_size = MSC_BLOCK_SIZE;
    }

    bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                               bool start, bool load_eject)
    {
        (void)lun;
        (void)power_condition;
        if (load_eject && !start)
        {
            // This is the "safely remove" the screen asks the user for.
            disk_ioctl(MSC_DRIVE, CTRL_SYNC, 0);
            ejected = true;
        }
        return true;
    }

    int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                              void *buffer, uint32_t bufsize)
    {
        (void)lun;
        if (offset != 0 || (bufsize % MSC_BLOCK_SIZE) != 0)
        {
            return -1;
        }
        if (lba + bufsize / MSC_BLOCK_SIZE > blockCount)
        {
            return -1;
        }
        if (disk_read(MSC_DRIVE, (BYTE *)buffer, lba, bufsize / MSC_BLOCK_SIZE) != RES_OK)
        {
            return -1;
        }
        return (int32_t)bufsize;
    }

    bool tud_msc_is_writable_cb(uint8_t lun)
    {
        (void)lun;
        return true;
    }

    int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                               uint8_t *buffer, uint32_t bufsize)
    {
        (void)lun;
        if (offset != 0 || (bufsize % MSC_BLOCK_SIZE) != 0)
        {
            return -1;
        }
        if (lba + bufsize / MSC_BLOCK_SIZE > blockCount)
        {
            return -1;
        }
        if (disk_write(MSC_DRIVE, buffer, lba, bufsize / MSC_BLOCK_SIZE) != RES_OK)
        {
            return -1;
        }
        // Tells the settings menu to make the browser re-list on the way out.
        mediaDirty = true;
        return (int32_t)bufsize;
    }

    int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                            void *buffer, uint16_t bufsize)
    {
        (void)lun;
        (void)buffer;
        (void)bufsize;
        // Nothing beyond the mandatory commands tinyusb already answers.
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        (void)scsi_cmd;
        return -1;
    }
}

#endif // FRENS_USB_MSC
