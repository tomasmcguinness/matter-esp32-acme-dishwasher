# Matter ESP32 ACME Dishwasher

This code creates a "working" dishwasher that can be connected to and controlled via the Matter Protocol. Powered by the ESP32 and designed for the Crowpanel 4.2" e-ink display , it supports most of the Matter Dishwasher devicetype with the mandatory OperationalState cluster as well as the optional OnOff and DishwasherMode clusters.

I've made a YouTube video which gives a demonstration: 
https://youtu.be/BGIULIQ3aqs

I've pull all the blog posts I have written on this subject into a single place
https://tomasmcguinness.com/tiny-matter-dishwasher/

## Hardware

It has two push buttons and one rotary encoder. Information is presented on an OLED screen.

* The first push button acts as the on/off button. Push it once to turn the device on and off. At present, it just controls the display.
* The second push button as a start/stop/pause/resume button.
* The rotary encoder allows the selection of DishwasherMode (aka program)

You can turn the device on, choose a program and press start. The device will start a 30 second countdown and work through five `Phases`. Once it's finished, it will stop. Whilst running, the program can be paused and resumed.

This is a work in progress, so the implementation isn't perfect and might not fully align to the Matter specification (Dead Front behaviour for example - I don't ignore commands when the device is off)

To reset the device, hold down to the On/Off button for 5 seconds and you'll see a prompt to "Reset the device". Click the appropriate button; On/Off for No, Start/Pause/Resume for Yes. This will reset the Matter fabric.

## Why?

I'm really interested in the energy management aspect of the Matter protocol. There aren't any devices on the market to enable me to explore this protocol and besides, I'm not going to buy a new applicance for testing! Having this toy dishwasher will let me play around with how the energy management might work.

## Wiring

This code has been built for the XIAO ESP32-C6. Currently, the pinouts are hardcoded, so if you want to use a different ESP Board, you'll need to change them in the code.

| GPIO     | Usage   |
| -------- | ------- |
| 01 | On/Off Button (active high) |
| 02 | Start/Pause/Resume Button (active high) |
| 22 | Display I2C SDA |
| 23 | Display I2C SCL |
| 16 | Display I2C Reset |
| 20 | Rotary Encoder Clk |
| 18 | Rotary Encoder DT |

## Building

To compile this application, you will need esp-idf v5.4.1 and esp-matter v1.4. As I'm using a C6, I set the target accordingly. Once you have setup your esp-matter environment, you can compile it like this.

```
idf.py set-target esp32-c6
idf.py build flash monitor
```

If you are using a diffent ESP32, change the target accordingly.

## Commissioning

To commission the device, follow the instuctions here https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html#commissioning-and-control

## Using

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

## Device Energy Management

I've made a start on this. It's still in its infancy, but when you start a cycle, the new Device Energy Management cluster will generate a forecast. The number of slots in the forecast mirrors the selected mode (Eco is one slot, Chef is 2 and Quick is three). It's filled with meaningless data for now. 

https://tomasmcguinness.com/2025/07/26/matter-tiny-dishwasher-adding-energy-forecast/
https://tomasmcguinness.com/2025/08/14/matter-fixing-the-resource_exhausted-error-in-the-energy-forecast/

## Things to do

- [ ] Display a QR code or setup code if device is uncommissioned.
- [ ] Implement the property DeadFront behaviour, where all changes are ignored whilst the device is off (I think!)
- [x] Reset Current Phase to 0 when Operational State is changed to Stopped.
- [x] Add energy management endpoint
- [x] Add basic energy forecast
- [ ] Create a hex file for easier flashing (without needing the whole ESP-IDF setup!)

## Feedback please!

If you have any suggestions, I'd love to hear them!

## Little Enclosure

To complete the effect, I've built a small enclosure in foam board.

![IMG_8493](https://github.com/user-attachments/assets/be44feea-0bbc-4ee8-a25a-3436a507302f)
