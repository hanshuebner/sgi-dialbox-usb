/* SGI Dialbox translator firmware (hans.huebner@gmail.com) */

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

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>

#include "Descriptors.h"
#include "uart.h"

#include <LUFA/Drivers/Board/LEDs.h>
#include <LUFA/Drivers/USB/USB.h>

#define BAUD_RATE 9600

#define LED_CONFIG	(DDRD |= (1<<6))
#define LED_ON		(PORTD |= (1<<6))
#define LED_OFF		(PORTD &= ~(1<<6))

/** LUFA MIDI Class driver interface configuration and state information. This structure is
 *  passed to all MIDI Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_MIDI_Device_t Keyboard_MIDI_Interface = {
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

void
jumpToLoader(void)
{
  // Jump to the HalfKay (or any other) boot loader

  cli();
  // disable watchdog, if enabled
  // disable all peripherals
  UDCON = 1;
  USBCON = (1<<FRZCLK);  // disable USB
  UCSR1B = 0;
  _delay_ms(5);
  EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
  TIMSK0 = 0; TIMSK1 = 0; TIMSK3 = 0; TIMSK4 = 0; UCSR1B = 0; TWCR = 0;
  DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0; TWCR = 0;
  PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
  asm volatile("jmp 0x7E00");
}

/** Configures the board hardware and chip peripherals */
void SetupHardware(void)
{
  /* Disable watchdog if enabled by bootloader/fuses */
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  /* Disable clock division */
  clock_prescale_set(clock_div_1);

  /* Hardware Initialization */
  LED_CONFIG;
  USB_Init();

  uart_init(BAUD_RATE);

  LED_ON;
  _delay_ms(1000);
  LED_OFF;
  uart_putchar(0x20);
  uart_putchar(0x50);
  uart_putchar(0x00);
  uart_putchar(0xFF);
  _delay_ms(100);
  LED_ON;
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


#define BASE_CC 20

void
processSerial(void)
{
  static uint8_t uartInputCount = 0;
  static uint8_t dialNumber;
  static uint16_t dialValue;
  static uint16_t oldDialValues[8];

  LED_ON;
  if (uart_available()) {
    int8_t c = uart_getchar();
    switch (uartInputCount) {
    case 0:
      if ((c >= 0x30) && (c < 0x38)) {
        dialNumber = c - 0x30;
        uartInputCount = 1;
      }
      LED_OFF;
      return; // note: early return
    case 1:
      dialValue = c << 8;
      uartInputCount = 2;
      return; // note: early return
    case 2:
      dialValue |= c;
    }
    // we got a new dial value at this point

    uartInputCount = 0;
    
    int16_t delta = dialValue - oldDialValues[dialNumber];

    while (delta) {
      int8_t value;
      if (delta > 0) {
        value = (delta > 63) ? 63 : delta;
      } else if (delta < 0) {
        value = (delta < -63) ? -63 : delta;
      }
      delta -= value;
      value &= 0x7f;
      sendMidiCc(BASE_CC + dialNumber, value);
    }
    MIDI_Device_Flush(&Keyboard_MIDI_Interface);

    oldDialValues[dialNumber] = dialValue;
  }
}

int
main(void)
{
  SetupHardware();

  GlobalInterruptEnable();

  uint8_t bootloaderChordCount = 0;
  const uint8_t bootloaderChordLength = 3;
  uint8_t bootloaderChord[] = { 0, 3, 5 };

  for (;;) {

    processSerial();
    
    MIDI_EventPacket_t ReceivedMIDIEvent;
    while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent)) {
      if ((ReceivedMIDIEvent.Event == MIDI_EVENT(0, MIDI_COMMAND_NOTE_ON))
          && (ReceivedMIDIEvent.Data3 > 0)) {
        uint8_t note = ReceivedMIDIEvent.Data2;
        if (note == bootloaderChord[bootloaderChordCount]) {
          bootloaderChordCount++;
          if (bootloaderChordCount == bootloaderChordLength) {
            jumpToLoader();
          }
        } else {
          bootloaderChordCount = 0;
        }
        LEDs_SetAllLEDs(ReceivedMIDIEvent.Data2 > 64 ? LEDS_LED1 : LEDS_LED2);
      } else {
        LEDs_SetAllLEDs(LEDS_NO_LEDS);
      }
    }

    MIDI_Device_USBTask(&Keyboard_MIDI_Interface);
    USB_USBTask();
  }
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
  bool ConfigSuccess = true;

  ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&Keyboard_MIDI_Interface);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
  MIDI_Device_ProcessControlRequest(&Keyboard_MIDI_Interface);
}

