#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/hid/HIDDriver.h"

#include "tusb.h"

namespace ThetaGP::Drivers::GPDriver {

GPDriverManager::GPDriverManager()
    : usbdevice(nullptr), inputMode(InputMode::None) {}

void GPDriverManager::setup(InputMode mode) {
  switch (mode) {
  case InputMode::HID:
    usbdevice = new HIDDriver();
    break;
  default:
    return;
  }

  if (usbdevice != nullptr) {
    usbdevice->initialize();
  }
  inputMode = mode;

  // TinyUSB initialize
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE,
#if defined(THETAGP_CFG_USB_HS)
                                 .speed = TUSB_SPEED_HIGH
#else
                                 .speed = TUSB_SPEED_FULL
#endif
  };
  tusb_init(1, &dev_init);
}

} // namespace ThetaGP::Drivers::GPDriver
