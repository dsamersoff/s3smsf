# SMS Forwarder on ESP32 and SIM800Lv2 or Linux and Huawei GSM Modem

_Russian version of this document is documents/README_RUS.md_

### Introduction

The goal of this project is to create a device that can forward SMS messages from one phone number to another. The internet is not used anywhere, which is a fundamental decision (see [Discussion]()).

The software part is written entirely from scratch in C and is designed to work either under Linux or FreeRTOS. This means it can be part of a setup consisting of any Unix-based computer and a USB modem, or function as a standalone device.

The hardware part of the standalone device consists of an ESP32-WROOM (there are many ESP32 variants, but it must have UART2 and support 5V power), a SIM800Lv2 module (the second version has a much better antenna and supports 5V power), and an SSD1306 I2C LCD display (128x64). The display is optional and is used to show the current status.

### Changes from the Previous Release

- A diode was added to the hardware to protect the ESP32 from power overload during modem startup.
- The MP102 board was removed and replaced with a simple Type-C connector.
- Support for reading and sending multipart (concatenated) SMS messages has been implemented.
- Code refactoring was performed: heap memory is now used for storing and processing messages instead of on-stack buffers, as the latter caused hard-to-trace issues on the ESP32.

### User Guide
- Purchase a SIM card with a plan that includes a sufficient number of SMS messages, in MINI-SIM format.
- Insert the SIM card into a phone and:
  - Remove the PIN code.
  - Delete all contacts from the SIM card.
  - Add a contact named **"PRIMARY NUMBER"** (with a space, without quotes) containing the phone number to which all SMS messages will be forwarded. The number must be in international format (e.g., +7 321 987 65 43).

#### For Standalone Device Users
- Prepare a power adapter, USB 5V 2A.
- Prepare a USB type-C cable.
- Insert the SIM card into the modem.
- Turn on the power.
- Wait a few minutes and ensure that the screen displays:
  - `DM\S <version>`
  - The name of your GSM operator.
  - The phone number for forwarding.
  - Scrolling lines showing the number of messages found.

#### For Linux and USB Modem Users
- Ensure that the modem is detected, for example, as `/dev/ttyUSB0`.
- Run the program:
  ```
  ./s3smsf -p <port>
  ```
  - The program can run in the background with the `-D` flag.
  - The forwarding phone number can be specified in the command line with `-a <phone>`.
  - You can execute maintenance command right from command line with `-c <command>` e.g. `-c "++CLEAR"
  - You can adjust verbosity level with `-v ` from 3 (ERROR) to 7 (DEBUG)
  - You can redirect log output to file with `-l <filename>`
  - Or to syslog with `-L`

SMS messages sent from the **PRIMARY NUMBER** can contain control commands. Command SMS messages are not forwarded.

The following commands are available:
- `++CLEAR`	Deletes all messages from the SIM.
- `++CONTACTS`	Dumps the first 25 contacts from the SIM to the console.
- `++DUMP`	Dumps all messages from the SIM to the console.
- `++DELETE <n>`	Enables/disables deletion of received messages (n is expected to be 0 or 1).
- `++EXPIRE <n>`	Enables/disables expiration support (n is expected to be 0 or 1).
- `++FORWARD <n>`	Enables/disables forwarding support (n is expected to be 0 or 1).
- `++HEADER <n>`	Enables/disables an additional header (n is expected to be 0 or 1).
- `++LOG <n>`	Sets verbosity level; e.g., ++LOG 7 enables debug output.
- `++MULTIPART <n>`	Enables/disables multipart SMS support (n is expected to be 0 or 1).
- `++SAVED`	Dumps all messages from the hash table to the console.

### Software Description
#### Compilation
**Dependencies:** cmake > 3.16, esp-idf (for FreeRTOS version)

**Linux:**
```
cd <project folder>
mkdir build
cd build
cmake -DTARGET=linux ..
make
```

**Linux (cross-compilation):**
```
cd <project folder>
mkdir build
cd build
cmake -DTARGET=linux -DCMAKE_C_COMPILER=<path to cross compiler> ..
make
```

**FreeRTOS:**
Install `esp-idf` according to the documentation.
```
cd <project folder>
idf.py build
idf.py flash
```

**Off-line Testing:**
```
cd <project folder>
mkdir build
cd build
cmake -DTARGET=moc
make
```

#### Source Code Structure
```
components/ - Additional RTOS components, including the SSD1306 driver
documents/  - Documentation
main/       - RTOS-specific files
main-linux/ - Linux-specific files
main-moc/   - Off-line testing-specific files
shared/     - Common files for all OS
README.md
```
Platform-dependent code is mainly located in `smsf-hal.c`.

#### Known Issues
SIM800Lv2 is sensitive to the power provided, not all USB power supply will work.

### Hardware Description
The main hardware components include an ESP32 microcontroller and a SIM800Lv2 modem. The modem is connected to the microcontroller via GPIO 27 (modem TX) and GPIO 25 (modem RX), which are configured as UART2 RX and TX, respectively. An I2C LCD display is also connected, using GPIO 21 for SDA and GPIO 22 for SCL, and is powered directly from the ESP32’s 3.3V output. The SIM800Lv2 modem, however, requires a dedicated 5V power source, as it can draw over 1A during peak network activity. To handle power surges, a 4700µF/16V capacitor is included in the circuit. Additionally, an RL207 diode is used to protect the ESP32 from potential overloads caused by the modem.

![s3smsf.png](docs/s3smsf.png)

### Discussion
1. A SIM card can store 10 to 15 messages. If the memory is full, new messages are no longer received. At the same time, there is no way to ensure a transactional operation — if message 4 is deleted successfully but message 5 is not, the next deletion attempt for message 5 might accidentally remove a newly received message. Therefore, messages are read one by one in a loop, and `CMGL` is not used.
2. WiFi and Telegram (or similar services) are neither reliable nor secure channels for SMS forwarding. Such forwarders are becoming popular, and sooner or later, they will become targets for fraudsters. Due to the limited capabilities of microcontrollers, protection and monitoring options are also very limited.

If you want to forward messages via the internet, it is better to build a solution based on a Raspberry Pi (which can use the same SIM800L or a different modem), integrate it with a dedicated mobile app, and use push notifications — similar to how banking apps work. However, such a solution is beyond the scope of this project.
