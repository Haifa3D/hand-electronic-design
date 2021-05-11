# This code is a demo code for any wireless system which translates the user's desires
# into hand movements. This code allows you to control the Oded Hand - a low cost, open
#  source prosthetic hand that anyone can build (https://github.com/Haifa3D) via BLE
#  protocol we developed for this purpose (For more information about the BLE protocol see
#  https://github.com/Haifa3D/haifa3d-hand-app/blob/master/Protocol.md)
#
#  In general, the Hand itself is the server and you UI system is the client.
#  Here we show how to communicate with the hand using a python code
#
#  Getting started:
#  add your device address (you can check your BLE device address on your mobile app)
#
#  now you need to decide how to communicate with the hand - you can use
#  the 'Execute Characteristic' (i.e. pRemoteCharExecute) and/or the 'Trigger
#  Characteristic' (i.e. pRemoteCharTrigger).
#  Here we show an example of triggering a pre-defined preset:
#
#   trigger_char_uuid = 'e0198002-7544-42c1-0001-b24344b6aa70'
#   preset_id = 0  //can be 0-11 since we have 12 defined presets in the Oded Hand
#   ble_client.write_gatt_char(trigger_char_uuid, bytes([preset_id]))
#   (to define your presets check out our mobile app: https://play.google.com/store/apps/details?id=com.gjung.haifa3d)


from bleak import BleakClient
import asyncio


# <<<<<<<<<<<<<<<<<<<<<< BLE connection function  >>>>>>>>>>>>>>>>>>>>>>> #
async def init_ble(address='24:62:AB:F2:AF:46', char_uuid='e0198002-7544-42c1-0001-b24344b6aa70'):

    ble_client = BleakClient(address)
    # connect the Hand
    if await ble_client.connect():
        print('Connected to Device: ' + address)
    # find the Trigger preset characteristic
    char_out = None
    for service in ble_client.services:
        for char in service.characteristics:
            if char.uuid == char_uuid:
                char_out = char.uuid
                print('Trigger char was found')
    if char_out is None:
        print("didn't found the relevant characteristic")
    return ble_client,  char_out


# <<<<<<<<<<<<<<<<<<<<<< send BLE message function  >>>>>>>>>>>>>>>>>>>>>>> #
async def send_ble_message(ble_client, char_write, value_write):
    await ble_client.write_gatt_char(char_write, bytes([value_write]))  # Sending Event via BLE


# <<<<<<<<<<<<<<<<<<<<<< BLE disconnection function  >>>>>>>>>>>>>>>>>>>>>>> #
async def disconnect_ble(ble_client):
    await ble_client.disconnect()

# init BLE connection:
my_device_address = '24:62:AB:F2:AF:46'   # change the address according to your device address
trigger_char_uuid = 'e0198002-7544-42c1-0001-b24344b6aa70'

loop = asyncio.get_event_loop()
client, trigger_char = loop.run_until_complete(init_ble(my_device_address, trigger_char_uuid))

# init main loop
activate_preset_id = 1  # here we activate only preset 1 but you can change it as you please (between 0-11)
is_event = False


while True:
    # do something

    if is_event:  # if an event was detected and a hand movement is needed
        loop.run_until_complete(send_ble_message(client, trigger_char, activate_preset_id))  # Sending to BLE
        print('event sent! preset: ', activate_preset_id)

