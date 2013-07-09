import rf_prot
import shelve
import time
import struct
from datetime import datetime
from exceptions import *

def enum(**enums):
    return type('Enum', (), enums)

COMMAND_TYPE = enum(device_init=0xf0, device_init_response=0xf1, sensor_data=0x10, screen_data=0x11)
DEVICE_TYPE = enum(screen=0x10, sensor=0x20)
DATA_TYPE = enum(date=0x1, string=0x2, bitmap=0x3)

def seconds(): return int(round(time.time()))

class App(object):
    APP_NAME = 'undef'
    def __init__(self, interval=6):
        self.last_time = 0
        self.interval = interval
        self.last_data = ''

    def get_data(self, _id):
        self.last_time = seconds()
        raise NotImplemented

    def should_run(self):
        if self.last_time + self.interval > seconds():
            return False
        return True

    def valid(self):
        if (self.last_time + (self.interval*10)) < seconds():
            return False
        return True

class DT(App):
    APP_NAME = 'time'
    def valid(self):
        return True
    def get_data(self, _id):
        self.last_time = seconds()
        dt = datetime.now().timetuple()[0:6]
        return chr(DATA_TYPE.date) + struct.pack('HBBBBB', *dt)
        
class ShortTest(App):
    APP_NAME='shorttest'
    def valid(self):
        return True
    def get_data(self, _id):
        self.last_time = seconds()
        return chr(DATA_TYPE.string) + chr(_id) + 'short test'
    
class LongTest(App):
    APP_NAME='longtest'
    def valid(self):
        return True
    def get_data(self,_id):
        self.last_time = seconds()
        return  chr(DATA_TYPE.string) + chr(_id) + 'long test' * 4



class Sensor(object):
    REP_STR = "Sensor data: {}"
    def __init__(self, _id):
        self._id = _id
        self.last_data = ''
        self.last_time = 0
        self.new_data = False

    def update(self, data):
        self.last_data = data
        self.last_time = seconds()
        self.new_data = True

    def get_data(self):
        REP_STR.format(*self.last_data)

    def is_new_data(self):
        if self.new_data:
            self.new_data = False
            return True
        return False

class RFExchange(object):
    def __init__(self, apps):
        #rx_addrs = [0xF0F0F0F01F, 0xF0F0F0F02F, 0xF0F0F0F03F, 0xF0F0F0F04F, 0xF0F0F0F05F]
        self._rf = rf_prot.RF24_Wrapper()
        self._db = shelve.open('rf_exchange_db')
        self.apps = apps
        if not self._db.has_key('init'):
            self._db['init'] = True
            self._db['current_pipe'] = 0
            self._db['in_devices'] = []
            self._db['out_devices'] = []
            self._db['app_data'] = {}
            self._db['sensors'] = {}

    def handle_rx_data(self, data):
        cmd = struct.unpack('B', data[0])[0]
        if cmd == COMMAND_TYPE.device_init:
            dev_type = struct.unpack('B', data[1])[0]
            addr = struct.unpack('Q', data[2:10])[0]
            print "got device init cmd: {}, dev type: {}, addr: {}".format(hex(cmd), hex(dev_type), hex(addr))
            if dev_type==DEVICE_TYPE.screen:
                if not addr in self._db['out_devices']:
                    print repr(addr)
                    x=self._db['out_devices']
                    x.append(addr)
                    self._db['out_devices'] = x
                data=chr(COMMAND_TYPE.device_init_response)+data[1:]
                time.sleep(0.05)
                self._rf.write(addr, data[:10])

    def send_data(self, data):
        for addr in self._db['out_devices']:
            print "sending data {} to device: {}".format(repr(data), addr)
            self._rf.write(addr, data)
                    
    def run(self):
        try:
            while True:
                (pipe, data) = self._rf.read(5000)
                if data:
                    self.handle_rx_data(data)
                changed = []
                for _id, app in enumerate(self.apps):
                    if app.should_run():
                        data = app.get_data(_id)
                        print repr(data)
                        if app.valid():
                            self.send_data(data)
                        else:
                            self.send_invalid(_id)
                        #changed.append(app.APP_NAME)
                for _id, sensor in self._db['sensors'].items():
                    if sensor.is_new_data:
                        changed.append(_id)
        finally:
            self._db.close()


RFExchange([DT(),LongTest()]).run()
