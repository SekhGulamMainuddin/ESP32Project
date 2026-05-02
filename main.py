import bluetooth
import time
from micropython import const

# BLE event codes (from ubluetooth docs)
_IRQ_CENTRAL_CONNECT    = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE        = const(3)
_IRQ_GATTS_READ_REQUEST = const(4)

# Characteristic flags
_FLAG_READ     = const(0x0002)
_FLAG_WRITE    = const(0x0008)
_FLAG_NOTIFY   = const(0x0010)

# UUIDs — replace with your own from uuidgenerator.net
SERVICE_UUID   = bluetooth.UUID("12345678-1234-1234-1234-123456789012")
CHAR_RW_UUID   = bluetooth.UUID("abcdef01-1234-1234-1234-abcdef012345")
CHAR_NTF_UUID  = bluetooth.UUID("abcdef02-1234-1234-1234-abcdef012345")

# Second service example, showing how to add new services / characteristics
SERVICE2_UUID  = bluetooth.UUID("87654321-4321-4321-4321-210987654321")
CHAR_CMD_UUID  = bluetooth.UUID("fedcba98-7654-3210-fedc-ba9876543210")


def advertising_payload(name):
    name_bytes = name.encode()
    return (
        bytes([2, 0x01, 0x06]) +          # Flags: LE General Discoverable
        bytes([len(name_bytes) + 1, 0x09]) +
        name_bytes
    )


class BLEServer:
    def __init__(self, name="EdgeHax-S3"):
        self._ble = bluetooth.BLE()
        self._ble.active(True)
        self._ble.irq(self._irq)
        self._connections = set()
        self._name = name

        # Register two services:
        #   - first service has a read/write characteristic and notify characteristic
        #   - second service is write-only, showing how to add a new service
        ((self._rw_handle, self._ntf_handle), (self._cmd_handle,),) = self._ble.gatts_register_services((
            (SERVICE_UUID, (
                (CHAR_RW_UUID,  _FLAG_READ | _FLAG_WRITE),
                (CHAR_NTF_UUID, _FLAG_READ | _FLAG_NOTIFY),
            )),
            (SERVICE2_UUID, (
                (CHAR_CMD_UUID, _FLAG_WRITE),
            )),
        ))

        self._ble.gatts_write(self._rw_handle, b"Hello from ESP32-S3")
        self._ble.gatts_write(self._cmd_handle, b"")

        self._advertise()
        print("BLE Server started, advertising as:", name)

    def _irq(self, event, data):
        if event == _IRQ_CENTRAL_CONNECT:
            conn_handle, _, _ = data
            self._connections.add(conn_handle)
            print("Connected: handle", conn_handle)

        elif event == _IRQ_CENTRAL_DISCONNECT:
            conn_handle, _, _ = data
            self._connections.discard(conn_handle)
            print("Disconnected: handle", conn_handle)
            self._advertise()

        elif event == _IRQ_GATTS_WRITE:
            conn_handle, attr_handle = data
            if attr_handle == self._rw_handle:
                value = self._ble.gatts_read(attr_handle)
                print("RW write:", value.decode("utf-8", "replace"))
                self._ble.gatts_write(self._rw_handle, value)
            elif attr_handle == self._cmd_handle:
                value = self._ble.gatts_read(attr_handle)
                print("Command write:", value.decode("utf-8", "replace"))

        elif event == _IRQ_GATTS_READ_REQUEST:
            conn_handle, attr_handle = data
            if attr_handle == self._rw_handle:
                self._ble.gatts_write(self._rw_handle, b"Read at %d" % time.ticks_ms())

    def notify(self, message):
        if isinstance(message, str):
            message = message.encode()
        for conn in self._connections:
            self._ble.gatts_notify(conn, self._ntf_handle, message)
            print("Notified connection %s: %s" % (conn, message))

    def _advertise(self):
        adv_data = advertising_payload(self._name)
        self._ble.gap_advertise(100_000, adv_data=adv_data)
        print("Advertising...")


# --- Main ---
server = BLEServer("EdgeHax-S3")
count = 0

if __name__ == "__main__":
    while True:
        count += 1
        server.notify("tick:%d" % count)
        time.sleep(2)
