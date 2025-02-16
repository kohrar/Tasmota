/*
  xsns_18_pms5003.ino - PMS3003, PMS5003, PMS7003 particle concentration sensor support for Tasmota

  Copyright (C) 2021  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_PMS5003
/*********************************************************************************************\
 * PlanTower PMS3003, PMS5003, PMS7003 particle concentration sensor
 * For background information see http://aqicn.org/sensor/pms5003-7003/ or
 * http://aqicn.org/sensor/pms3003/
 *
 * Hardware Serial will be selected if GPIO3 = [PMS5003]
 * You can either support PMS3003 or PMS5003-7003 at one time. To enable the PMS3003 support
 * you must enable the define PMS_MODEL_PMS3003 on your configuration file.
\*********************************************************************************************/

#define XSNS_18             18

#define D_WARMUP     "Warmup"
#define D_DISABLE    "Disabled"
#define D_SLEEP      "Sleep"
#define D_ACTIVE     "Active"

#include <TasmotaSerial.h>

#ifndef WARMUP_PERIOD
#define WARMUP_PERIOD 30          // Turn on PMSX003 XX-seconds before read in passive mode
#endif

TasmotaSerial *PmsSerial;

enum PmsState
{
  PMS_DISABLE,
  PMS_SLEEP,
  PMS_WARMUP,
  PMS_ACTIVE
};

struct PMS5003 {
  uint16_t time = 0;
  uint8_t type = 1;
  uint8_t valid = 0;
  uint8_t wake_mode = 1;
  PmsState state = PMS_DISABLE;
  bool discovery_triggered = false;
} Pms;

enum PmsCommands
{
  CMD_MODE_ACTIVE,
  CMD_SLEEP,
  CMD_WAKEUP,
  CMD_MODE_PASSIVE,
  CMD_READ_DATA
};

const uint8_t kPmsCommands[][7] PROGMEM = {
    //  0     1    2    3     4     5     6
    {0x42, 0x4D, 0xE1, 0x00, 0x01, 0x01, 0x71},  // pms_set_active_mode
    {0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73},  // pms_sleep
    {0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74},  // pms_wake
    {0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70},  // pms_set_passive_mode
    {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71}}; // pms_passive_mode_read

struct pmsX003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
#ifdef PMS_MODEL_PMS3003
  uint16_t reserved1, reserved2, reserved3;
#else
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
#endif  // PMS_MODEL_PMS3003
  uint16_t checksum;
} pms_data;

/*********************************************************************************************/

size_t PmsSendCmd(uint8_t command_id)
{
  return PmsSerial->write(kPmsCommands[command_id], sizeof(kPmsCommands[command_id]));
}

/*********************************************************************************************/

bool PmsReadData(void)
{
  if (! PmsSerial->available()) {
    return false;
  }
  while ((PmsSerial->peek() != 0x42) && PmsSerial->available()) {
    PmsSerial->read();
  }
#ifdef PMS_MODEL_PMS3003
  if (PmsSerial->available() < 24) {
#else
  if (PmsSerial->available() < 32) {
#endif  // PMS_MODEL_PMS3003
    return false;
  }

#ifdef PMS_MODEL_PMS3003
  uint8_t buffer[24];
  PmsSerial->readBytes(buffer, 24);
#else
  uint8_t buffer[32];
  PmsSerial->readBytes(buffer, 32);
#endif  // PMS_MODEL_PMS3003
  uint16_t sum = 0;
  PmsSerial->flush();  // Make room for another burst

#ifdef PMS_MODEL_PMS3003
  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, buffer, 24);
#else
  AddLogBuffer(LOG_LEVEL_DEBUG_MORE, buffer, 32);
#endif  // PMS_MODEL_PMS3003

  // get checksum ready
#ifdef PMS_MODEL_PMS3003
  for (uint32_t i = 0; i < 22; i++) {
#else
  for (uint32_t i = 0; i < 30; i++) {
#endif  // PMS_MODEL_PMS3003
    sum += buffer[i];
  }
  // The data comes in endian'd, this solves it so it works on all platforms
#ifdef PMS_MODEL_PMS3003
  uint16_t buffer_u16[12];
  for (uint32_t i = 0; i < 12; i++) {
#else
  uint16_t buffer_u16[15];
  for (uint32_t i = 0; i < 15; i++) {
#endif  // PMS_MODEL_PMS3003
    buffer_u16[i] = buffer[2 + i*2 + 1];
    buffer_u16[i] += (buffer[2 + i*2] << 8);
  }
#ifdef PMS_MODEL_PMS3003
  if (sum != buffer_u16[10]) {
#else
  if (sum != buffer_u16[14]) {
#endif  // PMS_MODEL_PMS3003
    AddLog(LOG_LEVEL_DEBUG, PSTR("PMS: " D_CHECKSUM_FAILURE));
    return false;
  }

#ifdef PMS_MODEL_PMS3003
  memcpy((void *)&pms_data, (void *)buffer_u16, 22);
#else
  memcpy((void *)&pms_data, (void *)buffer_u16, 30);
#endif  // PMS_MODEL_PMS3003
  Pms.valid = 10;

  if (!Pms.discovery_triggered) {
    TasmotaGlobal.discovery_counter = 1;      // Force discovery
    Pms.discovery_triggered = true;
  }

  return true;
}

bool PmsPassiveMode(void)
{
  return Settings->pms_wake_interval > WARMUP_PERIOD + Settings->pms_poll_interval;
}

// Active mode: Read sensor data continuously
void PmsSetActiveMode(void)
{
  PmsSendCmd(CMD_MODE_ACTIVE);
  PmsSendCmd(CMD_WAKEUP);

  Pms.wake_mode = 1; // device is always awake in active mode
  if (Pms.state != PMS_ACTIVE) Pms.state = PMS_WARMUP; // device should become active
  Pms.time = 0;
}

// Passive mode: Read while in poll state; sleep otherwise
void PmsSetPassiveMode(void) {
  PmsSendCmd(CMD_MODE_PASSIVE);
  PmsSendCmd(CMD_SLEEP);

  Pms.wake_mode = 0;
  Pms.state = PMS_SLEEP;
  Pms.time = 0;
}


/*********************************************************************************************\
 * Command Sensor18
 * 
 * Commands only work if TX pin is available.
 *
 * Takes 2 types of parameters:
 *  <wake interval>        - set wake interval, kept for backwards compatibility
 *  1,<wake interval>      - set wake interval
 *  2,<poll interval>      - set poll interval
 *
 * What do these intervals do? Here is a timeline:
 * 
 *      start -->|-- warmup --|-- poll interval --|-- sleep time --|--> (restart)
 *               |----------------  wake interval  ----------------|
 *
 *  * Warmup time for sensor is 30 seconds (WARMUP_PERIOD)
 *  * Setting the wake interval less than WARMUP_PERIOD + poll interval will result in 
 *    the sensor being in active mode (ie. constant sensor readings)
 *    
 *    Special wake interval values that can be used are:
 *    - 0           - Disable (put sensor to sleep indefinitely)
 *    - 1 .. N      - Active Mode (continuous sensor readings), where N = warmup + poll interval
 *    - N .. 65535  - Passive Mode (read sensor every x seconds), where N = warmup + poll interval
 *
 * Examples:
 *  1. To set the sensor to read for 1 minute every 5 minutes, set: wake interval = 300, poll interval = 60
 *  2. To set the sensor to read for 15 minutes every 1 hour, set: wake interval = 3600, poll interval = 900
 *  3. To set the sensor to read once minutes every 15 minutes, set: wake interval = 900, poll interval = 1
 *  4. To set the sensor to read continuously, set: wake interval = 1
 *
\*********************************************************************************************/

bool PmsCommandSensor(void)
{
  // handle arguments only if TX pin is available. Otherwise, sensor will be in active mode.
  if (PinUsed(GPIO_PMS5003_TX)) {

    // multi parameter
    if (ArgC() > 1) {
      char argument[XdrvMailbox.data_len];
      switch (XdrvMailbox.payload) {
        case 1:
          Settings->pms_wake_interval = (uint16_t)strtol(ArgV(argument, 2), nullptr, 10);
          break;

        case 2:
          Settings->pms_poll_interval = (uint16_t)strtol(ArgV(argument, 2), nullptr, 10);
          break;
      }

    // single parameter, with valid values
    } else if (XdrvMailbox.payload >= 0 && XdrvMailbox.payload <= 32000) {
      Settings->pms_wake_interval = XdrvMailbox.payload;
    }

    // Check wake interval value
    // 0: Disable sensor
    if (Settings->pms_wake_interval == 0) {
      Pms.state = PMS_DISABLE;
      Pms.valid = 0; // invalidate measurements
      PmsSendCmd(CMD_SLEEP);
    }

    // 1 .. WARMUP_PERIOD + poll interval: enable in active mode
    else if (Settings->pms_wake_interval <= WARMUP_PERIOD + Settings->pms_poll_interval) {
      PmsSetActiveMode();
    }

    // greater than WARMUP_PERIOD + poll interval: enable passive mode
    else if (Settings->pms_wake_interval > WARMUP_PERIOD + Settings->pms_poll_interval) {
      PmsSetPassiveMode();
      
      // minimum of 1 poll after device has warmed up, so we get at least 1 value
      if (Settings->pms_poll_interval == 0) {
        Settings->pms_poll_interval = 1;
      }
    }
  }

#ifdef PMS_MODEL_PMS3003
  Response_P(PSTR("{\"PMS3003\":{\"WakeInterval\":%d,\"PollInterval\":%d}}"),
    Settings->pms_wake_interval, Settings->pms_poll_interval);
#else
  Response_P(PSTR("{\"PMS5003\":{\"WakeInterval\":%d,\"PollInterval\":%d}}"),
    Settings->pms_wake_interval, Settings->pms_poll_interval);
#endif

  return true;
}


/*********************************************************************************************/

void PmsSecond(void)                 // Every second
{
    // do nothing if sensor is disabled
    if (Pms.state == PMS_DISABLE) return;

    // passive mode
  if (PmsPassiveMode()) {
    Pms.time++; // Advance the timer to keep track of what we need to do

    // wake interval time has elapsed; reset the timer so we start over (wake, poll, sleep)
    if (Pms.time > Settings->pms_wake_interval) {
      Pms.time = 0; // reset timer to start all over again
    }

    // The sensor should sleep after the warm up period and polling time has elapsed.
    // Put device to sleep for the remainder of the wake interval.
    if (Pms.time > WARMUP_PERIOD + Settings->pms_poll_interval) {
      PmsSendCmd(CMD_SLEEP);
      Pms.wake_mode = 0;
      Pms.state = PMS_SLEEP;
      return;
    }

    // Warmup period has elapsed or sensor is already ready.
    if (Pms.time > WARMUP_PERIOD || Pms.state == PMS_ACTIVE) {
      PmsSendCmd(CMD_READ_DATA);

      if (PmsReadData()) {
        Pms.state = PMS_ACTIVE;
        Pms.valid = 10; // mark data 'valid' for 10 more seconds
      } else {
        if (Pms.valid > 0) Pms.valid--;
      }
      return;
    }

    // At this point, we're waiting for the device to warm up (Pms.time <= WARMUP_PERIOD)
    // Send the wake command to the sensor only once
    if (Pms.wake_mode == 0) {
      Pms.wake_mode = 1;
      Pms.state = PMS_WARMUP;
      PmsSendCmd(CMD_WAKEUP);
    }

    return;
  }

  // active mode
  // Do nothing while we are still in the warmup period.
  if (Pms.time < WARMUP_PERIOD && Pms.state != PMS_ACTIVE) {
    Pms.time++; // Advance the timer to keep track that we are warming up
    Pms.state = PMS_WARMUP;
    return;
  }

  // Device should ready now in active mode. We just need to read data.
  if (PmsReadData()) {
    Pms.state = PMS_ACTIVE;
    Pms.valid = 10;
  } else {
    if (Pms.valid > 0) Pms.valid--;
  }
}

/*********************************************************************************************/

void PmsInit(void)
{
  Pms.type = 0;
  if (PinUsed(GPIO_PMS5003_RX)) {
    PmsSerial = new TasmotaSerial(Pin(GPIO_PMS5003_RX), (PinUsed(GPIO_PMS5003_TX)) ? Pin(GPIO_PMS5003_TX) : -1, 1);
    if (PmsSerial->begin(9600)) {
      if (PmsSerial->hardwareSerial()) { ClaimSerial(); }

      if (!PinUsed(GPIO_PMS5003_TX)) {  // setting interval not supported if TX pin not connected
        PmsSetActiveMode();
      } else {
        // TX pin in use, we can use passive mode if configured for it.
        if (PmsPassiveMode()) {
          PmsSetPassiveMode();
        } else {
          PmsSetActiveMode();
        }
      }

      Pms.type = 1;
    }
  }
}

#ifdef USE_WEBSERVER
#ifdef PMS_MODEL_PMS3003
const char HTTP_PMS3003_SNS[] PROGMEM =
//  "{s}PMS3003 " D_STANDARD_CONCENTRATION " 1 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
//  "{s}PMS3003 " D_STANDARD_CONCENTRATION " 2.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
//  "{s}PMS3003 " D_STANDARD_CONCENTRATION " 10 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS3003 " D_ENVIRONMENTAL_CONCENTRATION " 1 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS3003 " D_ENVIRONMENTAL_CONCENTRATION " 2.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS3003 " D_ENVIRONMENTAL_CONCENTRATION " 10 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}";
const char HTTP_PMS3003_SNS_STATE[] PROGMEM = "{s}PMS3003 State{m}%s{e}";
#else
const char HTTP_PMS5003_SNS[] PROGMEM =
//  "{s}PMS5003 " D_STANDARD_CONCENTRATION " 1 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
//  "{s}PMS5003 " D_STANDARD_CONCENTRATION " 2.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
//  "{s}PMS5003 " D_STANDARD_CONCENTRATION " 10 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS5003 " D_ENVIRONMENTAL_CONCENTRATION " 1 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS5003 " D_ENVIRONMENTAL_CONCENTRATION " 2.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS5003 " D_ENVIRONMENTAL_CONCENTRATION " 10 " D_UNIT_MICROMETER "{m}%d " D_UNIT_MICROGRAM_PER_CUBIC_METER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 0.3 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 0.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 1 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 2.5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 5 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}"
  "{s}PMS5003 " D_PARTICALS_BEYOND " 10 " D_UNIT_MICROMETER "{m}%d " D_UNIT_PARTS_PER_DECILITER "{e}";      // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>
const char HTTP_PMS5003_SNS_STATE[] PROGMEM = "{s}PMS5003 State{m}%s{e}";
#endif  // PMS_MODEL_PMS3003
#endif  // USE_WEBSERVER

void PmsShow(bool json)
{
  if (Pms.valid) {
    if (json) {
#ifdef PMS_MODEL_PMS3003
      ResponseAppend_P(PSTR(",\"PMS3003\":{\"CF1\":%d,\"CF2.5\":%d,\"CF10\":%d,\"PM1\":%d,\"PM2.5\":%d,\"PM10\":%d,\"State\":\"%s\"}"),
        pms_data.pm10_standard, pms_data.pm25_standard, pms_data.pm100_standard,
        pms_data.pm10_env, pms_data.pm25_env, pms_data.pm100_env,
        Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#else
      ResponseAppend_P(PSTR(",\"PMS5003\":{\"CF1\":%d,\"CF2.5\":%d,\"CF10\":%d,\"PM1\":%d,\"PM2.5\":%d,\"PM10\":%d,\"PB0.3\":%d,\"PB0.5\":%d,\"PB1\":%d,\"PB2.5\":%d,\"PB5\":%d,\"PB10\":%d,\"State\":\"%s\"}"),
        pms_data.pm10_standard, pms_data.pm25_standard, pms_data.pm100_standard,
        pms_data.pm10_env, pms_data.pm25_env, pms_data.pm100_env,
        pms_data.particles_03um, pms_data.particles_05um, pms_data.particles_10um, pms_data.particles_25um, pms_data.particles_50um, pms_data.particles_100um,
        Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#endif  // PMS_MODEL_PMS3003
#ifdef USE_DOMOTICZ
      if (0 == TasmotaGlobal.tele_period) {
        DomoticzSensor(DZ_COUNT, pms_data.pm10_env);     // PM1
        DomoticzSensor(DZ_VOLTAGE, pms_data.pm25_env);   // PM2.5
        DomoticzSensor(DZ_CURRENT, pms_data.pm100_env);  // PM10
      }
#endif  // USE_DOMOTICZ
#ifdef USE_WEBSERVER
    } else {

#ifdef PMS_MODEL_PMS3003
        WSContentSend_PD(HTTP_PMS3003_SNS,
//        pms_data.pm10_standard, pms_data.pm25_standard, pms_data.pm100_standard,
        pms_data.pm10_env, pms_data.pm25_env, pms_data.pm100_env);
        WSContentSend_PD(HTTP_PMS3003_SNS_STATE,
          Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#else
        WSContentSend_PD(HTTP_PMS5003_SNS,
//        pms_data.pm10_standard, pms_data.pm25_standard, pms_data.pm100_standard,
        pms_data.pm10_env, pms_data.pm25_env, pms_data.pm100_env,
        pms_data.particles_03um, pms_data.particles_05um, pms_data.particles_10um, pms_data.particles_25um, pms_data.particles_50um, pms_data.particles_100um);
        WSContentSend_PD(HTTP_PMS5003_SNS_STATE,
          Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#endif  // PMS_MODEL_PMS3003
#endif  // USE_WEBSERVER
    }

    return;
  }

  // not Pms.valid; measurements expired and no new data can be obtained (outside of sleep)
#ifdef USE_WEBSERVER
  if ( ! json) {
#ifdef PMS_MODEL_PMS3003
      WSContentSend_PD(HTTP_PMS3003_SNS_STATE,
        Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#else
      WSContentSend_PD(HTTP_PMS5003_SNS_STATE,
        Pms.state == PMS_DISABLE ? PSTR(D_DISABLE) : Pms.state == PMS_SLEEP ? PSTR(D_SLEEP) : Pms.state == PMS_WARMUP ? PSTR(D_WARMUP) : PSTR(D_ACTIVE));
#endif  // PMS_MODEL_PMS3003
  }
#endif  // USE_WEBSERVER
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns18(uint8_t function)
{
  bool result = false;

  if (Pms.type) {
    switch (function) {
      case FUNC_INIT:
        PmsInit();
        break;
      case FUNC_EVERY_SECOND:
        PmsSecond();
        break;
      case FUNC_COMMAND_SENSOR:
        if (XSNS_18 == XdrvMailbox.index) {
          result = PmsCommandSensor();
        }
        break;
      case FUNC_JSON_APPEND:
        PmsShow(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        PmsShow(0);
        break;
#endif  // USE_WEBSERVER
    }
  }
  return result;
}

#endif  // USE_PMS5003
