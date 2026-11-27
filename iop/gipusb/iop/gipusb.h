#ifndef _GIPUSB_H_
#define _GIPUSB_H_

#include "irx.h"

// Game Input Protocol (GIP), used by Xbox One / Series style wired pads.
// Interface signature: class 0xFF, subclass 0x47, protocol 0xD0.
// Packet layout: [cmd, options, sequence, payload length, payload...].
// Magic numbers follow linux drivers/input/joystick/xpad.c.
#define GIP_INTF_CLASS    0xFF
#define GIP_INTF_SUBCLASS 0x47
#define GIP_INTF_PROTOCOL 0xD0

#define GIP_CMD_POWER     0x05
#define GIP_CMD_INPUT     0x20
#define GIP_CMD_RUMBLE    0x09
#define GIP_MOTOR_ALL     0x0F
#define GIP_OPT_INTERNAL  0x20
#define GIP_SEQ0          0x00
#define GIP_PWR_ON        0x00
#define GIP_INPUT_MIN_LEN 14  // payload bytes of an input report

#define GIPUSB_PAD_SIZE   18  // size of the PS2 padButtonStatus layout copied to the EE
#define GIP_BUF_SIZE      64  // interrupt endpoint packet size

// Analog trigger value (0..255 after >> 2) that counts as a digital press.
#define GIP_TRIGGER_DIGITAL 32

enum eGIPUSBStatus {
    GIPUSB_STATE_DISCONNECTED = 0x00,
    GIPUSB_STATE_AUTHORIZED = 0x01,
    GIPUSB_STATE_CONFIGURED = 0x02,
    GIPUSB_STATE_CONNECTED = 0x04,
    GIPUSB_STATE_RUNNING = 0x08,
};

typedef struct _usb_gip
{
    int devId;
    int lock;
    int controlEndp;
    int inEndp;
    int outEndp;
    u8 status;
    u8 data[GIPUSB_PAD_SIZE];
} gipusb_device;

#endif
