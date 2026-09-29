#pragma once

// Releases the ESP32-S3 USB-OTG PHY and reconnects the hardware USB
// Serial/JTAG peripheral before a normal application restart (after USB Drive).
void handoffUsbOtgToSerialJtag();
