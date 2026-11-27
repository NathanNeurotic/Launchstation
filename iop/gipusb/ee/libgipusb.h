#ifndef LIBGIPUSB_H
#define LIBGIPUSB_H

#include <tamtypes.h>

enum eGIPUSBStatus {
    GIPUSB_STATE_DISCONNECTED = 0x00,
    GIPUSB_STATE_AUTHORIZED = 0x01,
    GIPUSB_STATE_CONFIGURED = 0x02,
    GIPUSB_STATE_CONNECTED = 0x04,
    GIPUSB_STATE_RUNNING = 0x08,
};

// Returns 1 when the IOP module answered the RPC bind, 0 otherwise.
int gipusb_init(void);
int gipusb_get_status(int port);
// Fills 18 bytes in the PS2 padButtonStatus layout. Returns 1 on success.
int gipusb_get_data(int port, u8 *data);
// Sets both motors (0-255 each). Sent to the pad only when the values change.
int gipusb_set_rumble(int port, u8 strong, u8 weak);

#endif /* LIBGIPUSB_H */
