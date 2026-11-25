# Background

I've had a water-based Under Floor Heating (UFH) system since January 2012. It's based on Wavin manifolds, Heatmiser control systems and Hep2o piping. 
Upstairs there is a 7-port manifold, with each thermostat operating a [2-wire Wavin Hep2o Underfloor Heating Actuator 24v](https://wavin.com/gb/p/3032b795-6b83-49b0-920c-e575bf335919/ufh-actuator-24v-2-wire/r). 
Downstairs there is an 8-port manifold, again with each 24v valve operated by a thermostat. 
All the thermostats upstairs are on a Cat5 UTP (4-twisted pair) bus. 
The upstairs wiring centre ([UH1 8-Zone Wiring Centre](https://www.heatmiser.com/download/211/english/7551/uh1-manual.pdf)) then connects to the downstairs UH1 wiring centre, 
which also is wired to all the thermostats.

## Heatmiser PRT-N

This [PRT-N](https://www.heatmiser.com/wp-content/uploads/bsk-pdf-manager/2019/10/PRT_PRT-N.pdf) thermostat operates on a half-duplex RS485 bus, that is, 
do not speak unless spoken to (by a console/control node). The thermostats operate autonomously and need no direction from the control node. 
The control node merely makes it easy to  adapt settings from a central point. The thermostat requires 3-pairs (6 wires);
- RS485 Bus (denoted by Y(ellow) and B(lue) wires)
- 12V Supply
- Heating (Dry contact) relay to operate 24v Valve

## Heatmiser PRT/HW-N

This [PRT/HW-N](https://www.heatmiser.com/wp-content/uploads/bsk-pdf-manager/2019/10/PRTHW_PRTHW-N.pdf) thermostat is 
identical to the PRT-N aside from it additionally controlling the hot water. All 4-pairs of the CAT5 are used;
- RS485 Bus (denoted by Y(ellow) and B(lue) wires)
- 12V Supply
- Heating (Dry Contact) relay to operate 24v Valve
- Hot Water (Dry Contact) relay

## Heatmiser Touch Pad (2010)

A 12V DC central control unit designed for Heatmiser 12V network thermostats, managing up to 32 heating and hot water zones. 
It contained a small Colour Touch Screen (58 x 77 mm) with a stylus and allowed you to remotely program the comfort levels of all networked
thermostats from one central point and copy program settings from one thermostat to another across the network.
The control unit contains a Samsung S3C44B0X01L 16/32-bit RISC microprocessor built on an ARM7TDMI core, designed for embedded and low-power applications.

### Key Specifications

- Core & Architecture: ARM7TDMI core, SAMBA II bus architecture, 8KB cache
- Clock Speed: Up to 66 MHz
- Operating Voltage: 2.5V core; 3.0V to 3.6V I/O
- Display & Audio Interfaces: LCD controller (up to 256-color DSTN), IIS-BUS
- Connectivity & Peripherals: 2x UART with handshake, 4-ch DMA, 5-ch PWM timers, 8-ch 10-bit ADC, IIC-BUS, synchronous SIO, RTC, I/O ports, PLL clock

# 21st Century

The UFH system is more or less set it and forget. Everything is on a timer, for weekdays, for weekends, etc. 
A UFH system cannot operate like a traditional heating system, as it's not very responsive.
You more or less have to have it on all the time, keeping the house at the minimum desired setting. I would guess
that if the house was completely cold and you needed to heat it from that point, it would probably take you a week.
Whilst it sounds dreadfully inefficient, our heating bills for our extended 12+ rooms, were less than the original 3-bedroom 8-room house with traditional radiators.
However, as the years passed and Internet, mobile phones, apps, etc became prevalent, and everyone was monitoring and controlling their home remotely,
I was unable to do either. So, I set about on my journey to bring the system into the future. I tried;
- Creating a ZWave controller that would virtualise the existing thermostats
- Creating my own RS485 thermostat that also spoke wifi
  - The thought of even succeeding with one and then having to recreate it another 11 times was disheartening
- Creating a Zigbee controller that could interface with the RS485 bus.
- Create software on the PC that spoke to a TCP/IP server-> RS485
- Creating a new wiring centre
- and more

I've probably spent about 10 years on the above but never getting to the finishing line.
Then about two weeks ago, having resisted AI for coding (I've been honing my software development craft for 50 years)
I decided to have an initial conversation with Claude. We started out with the **/design** skill, designing look n' feel, fonts, styles, etc, and then,
produced the workflow/storyboard screen. Claude wired up all the screens so I could navigate and get a feel. At this point, no code written.
Then Claude created a a staged design document, with milestones and gates and we've been walking it through ever since.
The repos now supports the following;
- Replacement of the LCD Control Node with a sleek 7" colour touch display, running on an ESP32-S3.
- Wifi/Internet Access
- Matter Bridge
  - Virtual Thermostats
  - Connectivity to Aqara Hub M3
  - Connectivity to Apple Home (and Siri)
- Autosync of any thermostat clock that drifts
- RS485 Bus Diagnostics display
- Over the Air (OTA) updates
