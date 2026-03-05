# One button remote сontrol for Sony a6400 based on ESP32-C3_Mini

This is pre-pre-alpha version of my Remote Control for Sony a6400 on ESP32-C3.

The demo is based on <https://github.com/dzwiedziu-nkg/esp32-a7iv-rc>

My setup:

* Environment: Visual Studio Code with PlatormIO
* Device: ESP32-C3 Super Mini Dev Module
* One button, connecting the ground to the pin 1, with 10kOm pull up rezistor.
* Camera: Sony a6400
* SHOOT_BUTTON GPIO1
* FOCUS_BUTTON GPIO3
* BLUE_LIGTH GPIO7
* GREEN_LIGTH GPIO6

How this pre demo works:

1. Boot.
2. Search for "ILCE-6400" bluetooth device.
3. If found it pairs with it.
4. If focus button is pressed, it takes a focus.
5. If pressed focus button and shooting one together, it takes a photo.
6. In theory the blue light have to blink during pairing process, but it is not :-), need to fix it.

It should works with other Sony Alpha cameras but you must enter your cam `bleServerName`.

If it helped to you and saved you a few hours, please buy a coffee for a Michał Niedźwiecki, the person who implemented the code:
<https://www.paypal.com/paypalme/nkgmn/10>
