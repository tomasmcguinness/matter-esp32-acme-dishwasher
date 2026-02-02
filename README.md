# Matter ESP32 ACME Dishwasher

This code creates a "working" dishwasher that can be connected to and controlled via the Matter Protocol. Powered by the ESP32 and designed for the Crowpanel 4.2" e-ink display , it supports most of the Matter Dishwasher devicetype with the mandatory OperationalState cluster as well as the optional OnOff and DishwasherMode clusters.

> [!note]
> This project started as the Tiny Dishwasher. You can find the original repository over here - https://github.com/tomasmcguinness/matter-esp32-tiny-dishwasher
 
I've made a YouTube video which gives a demonstration: 
https://youtu.be/BGIULIQ3aqs

I've pull all the blog posts I have written on this subject into a single place
https://tomasmcguinness.com/tiny-matter-dishwasher/

## Hardware

This project is designed to run on the CrowPanel 4.2" e-ink display.

* The first push button acts as the on/off button. Push it once to turn the device on and off. At present, it just controls the display.
* The second push button as a start/stop/pause/resume button.
* The up/down/push dial allows the selection of DishwasherMode (aka program)

## Building

To compile this application, you will need esp-idf v5.4.1 and esp-matter v1.4. The CrowPanel contains an esp32-s3, so you need to set the target accordingly. Once you have setup your esp-matter environment, you can compile it like this.

```
idf.py set-target esp32-s3
idf.py build flash monitor
```

## Commissioning

Once you flash the code onto the device and power it up, you should be presented with a Matter Pairing QR Code.

<img width="500" alt="image" src="https://github.com/user-attachments/assets/0833e262-1ca0-4603-98ea-51d86e6ddff0" />

To commission the device, follow the instuctions here https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html#commissioning-and-control

## Using

Once you commission the device, you will see the status and selected program information.

<img width="500" alt="image" src="https://github.com/user-attachments/assets/c44d6992-f89f-47dd-b919-66cd05e12226" />

You can use the up/down dial on the side to change the selected program. The top side button will start/pause the program. 

I use the `chip-tool` for most testing, since Dishwashers (or any applicances) aren't supported in iOS Home or Google Home. 

The Aqara M100 hub has rudimentary support for Matter appliances. You can start/stop/pause/resume.

Some example `chip-tool` commands (based on a NodeId of 0x05) for turning it on, selecting a program and starting it.

```
chip-tool onoff on 0x05 0x01
chip-tool dishwashermode read supported-modes 0x05 0x01
chip-tool dishwashermode change-to-mode 1 0x05 0x01
chip-tool operationalstate start 0x05 0x01
```

As you execute these commands, the UI on the dishwasher would reflect them.

## Why?

I'm really interested in the energy management aspect of the Matter protocol. There aren't any devices on the market to enable me to explore this protocol and besides, I'm not going to buy a new applicance for testing! Having this toy dishwasher will let me play around with how the energy management might work.

## Adapting to other devices.

If you are using a diffent ESP32, change the target accordingly. You can 

## Device Energy Management

I've made a start on this. It's still in its infancy, but when you start a cycle, the new Device Energy Management cluster will generate a forecast. The number of slots in the forecast mirrors the selected mode (Eco is one slot, Chef is 2 and Quick is three). It's filled with meaningless data for now. 

https://tomasmcguinness.com/2025/07/26/matter-tiny-dishwasher-adding-energy-forecast/
https://tomasmcguinness.com/2025/08/14/matter-fixing-the-resource_exhausted-error-in-the-energy-forecast/

## Things to do

- [x] Display a QR code or setup code if device is uncommissioned.
- [ ] Implement the property DeadFront behaviour, where all changes are ignored whilst the device is off (I think!)
- [x] Reset Current Phase to 0 when Operational State is changed to Stopped.
- [x] Add energy management endpoint
- [x] Add basic energy forecast
- [ ] Create a hex file for easier flashing (without needing the whole ESP-IDF setup!)

## Feedback please!

If you have any suggestions, I'd love to hear them!
