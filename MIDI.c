/*
  LUFA Library
  Copyright (C) Dean Camera, 2013.

  dean [at] fourwalledcubicle [dot] com
  www.lufa-lib.org
*/

/*
  Copyright 2013  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the MIDI demo. This file contains the main tasks of
 *  the demo and is responsible for the initial application hardware configuration.
 */

#include "MIDI.h"

/** LUFA MIDI Class driver interface configuration and state information. This structure is
 *  passed to all MIDI Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_MIDI_Device_t Keyboard_MIDI_Interface =
  {
    .Config =
    {
      .StreamingInterfaceNumber = 1,
      .DataINEndpoint           =
      {
        .Address          = MIDI_STREAM_IN_EPADDR,
        .Size             = MIDI_STREAM_EPSIZE,
        .Banks            = 1,
      },
      .DataOUTEndpoint          =
      {
        .Address          = MIDI_STREAM_OUT_EPADDR,
        .Size             = MIDI_STREAM_EPSIZE,
        .Banks            = 1,
      },
    },
  };


/** Configures the board hardware and chip peripherals */
void SetupHardware(void)
{
  /* Disable watchdog if enabled by bootloader/fuses */
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  /* Disable clock division */
  clock_prescale_set(clock_div_1);

  /* Hardware Initialization */
  LEDs_Init();
  USB_Init();

  DDRD = (1 << PD1) | (1 << PD2) | (1 << PD3);
  PORTD = 1;
  PORTF = 1;

  PORTD |= (1 << PD2) | (1 << PD3);

  TCCR0B = (1 << CS02) | (1 << CS00);
}

int8_t counters[8];

// IRQ handler for timer IRQ - Increment all counters by one unless they're at 0x7FFF

uint16_t oldState;

void
checkForEdges(uint16_t state)
{
  uint16_t changed = state ^ oldState;
  for (int i = 0; i < 8; i++) {
    if ((changed & 1) && (counters[i] < 0x7f)) {
      counters[i]++;
    } else if ((changed & 2) && (counters[i] > 0x7f)) {
      counters[i]--;
    }
    changed <<= 2;
  }
  oldState = state;
}

uint16_t
pollShiftRegisters(void)
{
  // create load pulse on D0
  PORTD &= ~(1 << PD2);
  PORTD |= (1 << PD2);

  // read data bits
  uint8_t accu1 = 0;
  uint8_t accu2 = 0;
  for (int i = 0; i < 8; i++) {
    accu1 <<= 1;
    accu1 |= PIND & 1;
    accu2 <<= 1;
    accu2 |= PINF & 1;
    PORTD &= ~(1 << PD3);
    PORTD |= (1 << PD3);
  }

  return (accu2 << 8) | accu1;
}

#define MIDI_COMMAND_CC 0xb0

void
sendMidiCc(uint8_t ccNumber, uint8_t value)
{
  MIDI_EventPacket_t MIDIEvent = (MIDI_EventPacket_t) {
    .Event       = MIDI_EVENT(0, MIDI_COMMAND_CC),

    .Data1       = MIDI_COMMAND_CC,
    .Data2       = ccNumber,
    .Data3       = value
  };

  MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &MIDIEvent);
}

#define BASE_CC 102

void
checkSendTimer(void)
{
  if (TIFR0 & (1 << TOV0)) {
    PORTD &= ~(1 << PD1);

    TIFR0 |= (1 << TOV0);
    bool sent = false;
    for (uint8_t i = 0; i < 8; i++) {
      if (counters[i]) {
        sendMidiCc(BASE_CC + (i << 1) + ((counters[i] > 0) ? 1 : 0),
                   (counters[i] > 0) ? counters[i] : -counters[i]);
        counters[i] = 0;
        sent = true;
      }
    }
    if (sent) {
      MIDI_Device_Flush(&Keyboard_MIDI_Interface);
    }

    PORTD |= (1 << PD1);
  }
}
/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void)
{
  SetupHardware();

  LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
  GlobalInterruptEnable();

  for (;;) {
    uint16_t state = pollShiftRegisters();
    checkForEdges(state);
    checkSendTimer();

    MIDI_EventPacket_t ReceivedMIDIEvent;
    while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent)) {
      if ((ReceivedMIDIEvent.Event == MIDI_EVENT(0, MIDI_COMMAND_NOTE_ON)) && (ReceivedMIDIEvent.Data3 > 0)) {
        LEDs_SetAllLEDs(ReceivedMIDIEvent.Data2 > 64 ? LEDS_LED1 : LEDS_LED2);
      } else {
        LEDs_SetAllLEDs(LEDS_NO_LEDS);
      }
    }

    MIDI_Device_USBTask(&Keyboard_MIDI_Interface);
    USB_USBTask();
  }
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void)
{
  LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void)
{
  LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
  bool ConfigSuccess = true;

  ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&Keyboard_MIDI_Interface);

  LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
  MIDI_Device_ProcessControlRequest(&Keyboard_MIDI_Interface);
}

