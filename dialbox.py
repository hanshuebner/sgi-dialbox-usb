#!/usr/bin/env python

import select
import os
import fcntl
import struct
import exceptions
from time import sleep


#struct input_event {
#        struct timeval time; = {long seconds, long microseconds}
#        unsigned short type;
#        unsigned short code;
#        unsigned int value;
#};

input_event_struct = "@llHHi"
input_event_size = struct.calcsize(input_event_struct)

EVENT_BUTTON_PRESS = 1
EVENT_RELATIVE_MOTION = 2
RELATIVE_AXES_DIAL = 7
BUTTON_MISC = 0x100


# these are to select the appropriate kind of dialbox you use
SGI = 1
SPECTRAGRAPHICS = 2

class dialbox:
    def __init__(self, dev=None, timeout=1000, model=SGI):
        try:
            import serial
        except:
            print 'eventio-error: dialbox driver needs the serial module (http://pyserial.sf.net)'

        if dev is None:
            return None

        try:
		DEV = '/dev/ttyS%d' % int(dev)
        except:
		DEV = str(dev)

        # dialbox commands 
        self.DIAL_INITIALIZE  = "%c%c%c%c" % (0x20, 0x50, 0x00, 0xFF)
        self.DIAL_BASE         = 0x30
        self.DIAL_DELTA_BASE   = 0x40

        try:
            self.serial = serial.Serial(DEV,9600, timeout=timeout/1000)
        except serial.SerialException:
            print 'eventio-error: Unable to find dialbox. Is it connected? To the right port?'
            return None

        if model == SGI or model == SPECTRAGRAPHICS:
            self.model = model
        else:
            print 'eventio-error: dialbox model not recognized'
            return None

        # self.dial used by SGI to turn absolute into relative events
        self.dial = [0,0,0,0, 0,0,0,0]
        self.serial.flushInput()
        self.serial.flushOutput()
        self.serial.write(self.DIAL_INITIALIZE)
        sleep(timeout/1000) # check if can do without
        self.read_bytes()
        print "eventio-info: dialbox: initialized."

    def __del__(self):
        self.serial.close()

    # read and print whatever is left on the serial line
    def read_bytes(self):
        n=self.serial.inWaiting()
        if n == 0:
            return None
        format = '@%dB' % n
        data = self.serial.read(n)
        #print struct.unpack(format,data[:n])

    def waitevent(self):
        data = self.serial.read(3)
        if len(data) != 3 : return None
        (b1,b2,b3)=struct.unpack('@BBB',data)
        (val,)  =struct.unpack('>H',data[1:3])
        dial = b1 - self.DIAL_BASE
        
        if dial < 0 or dial > 7:
            print 'dialbox: missed a few bytes'
            return None

        if self.model == SGI:
            value = val - self.dial[dial]
            self.dial[dial] = val
        elif self.model == SPECTRAGRAPHICS:
            value = b2-b3
        else:
            return None
        return (dial,val) #ue


class queue:
    def __init__(self, dev=None, timeout=1):
        self.timeout = 1000 * timeout
        self.handle = -1
        if dev:
            if not self.OpenDevice(dev):
                raise exceptions.RuntimeError, 'eventio-error: unable to find requested device'
        else:
            ok = 0
            for d in range(0, 16):
                if self.OpenDevice("/dev/input/event%d" % d):
                    ok = 1
                    break
            if not ok:
                raise exceptions.RuntimeError, 'Unable to find powermate'
        self.poll = select.poll()
        self.poll.register(self.handle, select.POLLIN)
        self.event_queue = [] # queue used to reduce kernel/userspace context switching

    def __del__(self):
        if self.handle >= 0:
            self.poll.unregister(self.handle)
            os.close(self.handle)
            self.handle = -1
            del self.poll
            
    def OpenDevice(self, filename):
        try:
            self.handle = os.open(filename, os.O_RDWR)
            if self.handle < 0:
                return 0
            fcntl.fcntl(self.handle, fcntl.F_SETFL, os.O_NDELAY)
            return 1
        except exceptions.OSError:
            return 0

    def waitevent(self): # timeout in seconds
        if len(self.event_queue) > 0:
            return self.event_queue.pop(0)
        if self.handle < 0:
            return None
        r = self.poll.poll(int(self.timeout))
        if len(r) == 0:
            return None
        return self.GetEvent()
        
    def GetEvent(self): # only call when descriptor is readable
        if self.handle < 0:
            return None
        try:
            data = os.read(self.handle, input_event_size * 32)
            while data != '':
                self.event_queue.append(struct.unpack(input_event_struct, data[0:input_event_size]))
                data = data[input_event_size:]
            return self.event_queue.pop(0)
        except exceptions.OSError, e: # Errno 11: Resource temporarily unavailable
            #if e.errno == 19: # device has been disconnected
            #    report("PowerMate disconnected! Urgent!");
            return None

def usage():
    print 'usage: %s DEVICE (where EV_QUEUE is either /dev/input/event* for USB HID devices or /dev/ttyS* for serial line devices)' % (argv[0])
    exit()

if __name__ == "__main__":

    import re
    from sys import *
    from math import *
    if len(argv) == 2:
        if re.match('^/dev/input/event\d+$', argv[1]):
            print 'Initialized event queue: %s' % argv[1]
            fifo = queue(dev=argv[1])
        elif re.match('^/dev/(ttyS\d+|cu\..*)$', argv[1]):
            print 'Initialized serial line: %s' % argv[1]
            fifo = dialbox(dev=argv[1], model=SGI)
        else:
            usage()
    else:
        usage

    if fifo == None:
        print 'could not open device'
        usage()

    xy = [0.0,0.0]
    count = 0
    max=0
    while 1:
        event = fifo.waitevent()
        if event == None: continue
        code,value = event

        print '%d => %4d' % (code,value)
        continue



