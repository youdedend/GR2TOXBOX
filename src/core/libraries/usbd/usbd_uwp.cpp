// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// libSceUsbd for the console build.
//
// The desktop implementation in usbd.cpp is libusb end to end: enumeration,
// configuration, the transfer API and the event loop are all libusb calls, and
// the emulated figure-portal backends are libusb backends too (they fabricate a
// single device but still go through libusb for descriptors and transfers).
// libusb's Windows port is WinUSB plus SetupAPI/CfgMgr32, neither of which is in
// the onecore surface an AppContainer can link against, and upstream libusb has
// no UWP branch -- so there is nothing to fix here, only something to omit.
//
// What a console reports with nothing plugged into its USB ports is an empty
// device list. That is what this file implements, rather than the alternative of
// not registering libSceUsbd at all: a guest that links against the library and
// calls sceUsbdInit should get a working, empty USB stack and go on to its own
// "no portal attached" path, not fail to resolve the import at load time.
//
// The figure-portal emulation is the only thing that changes shape. On the
// console it is absent: the portals are a USB peripheral, there is no virtual one
// to bridge to, and the pad input the console does have comes from
// Windows.Gaming.Input (see src/uwp/). LoadFigure and friends therefore have no
// receiver, which is why ipc.cpp guards its USB_* IPC commands for this build.

#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "usbd.h"

namespace Libraries::Usbd {

// libusb's LIBUSB_ERROR_NOT_FOUND, which is what its own enumeration returns for
// "no such device" and what the desktop build surfaces through
// libusb_to_orbis_error as 0x80240000 - (-5).
static constexpr s32 SCE_USBD_ERROR_NOT_FOUND = 0x80240005;
static constexpr s32 SCE_USBD_ERROR_NOT_SUPPORTED = 0x80240012;

// Every entry point takes an opaque SceUsbdDevice/Handle/Transfer, which the
// caller can only have obtained from sceUsbdGetDeviceList or sceUsbdOpen -- and
// both fail here, so none of these are reachable with a valid pointer. They are
// still defined, with libusb's error values, so that a guest which ignores the
// failure of GetDeviceList and passes garbage gets a clean error rather than a
// crash inside a null dereference.

s32 PS4_SYSV_ABI sceUsbdInit() {
    LOG_INFO(Lib_Usbd, "USB stack initialised with no host controller (console build)");
    return ORBIS_OK;
}

void PS4_SYSV_ABI sceUsbdExit() {}

s64 PS4_SYSV_ABI sceUsbdGetDeviceList(SceUsbdDevice*** list) {
    // libusb_get_device_list returns NULL(-less) zero for an empty list, and the
    // caller then iterates a null-terminated array it is handed here.
    if (list != nullptr) {
        auto** empty = static_cast<SceUsbdDevice**>(calloc(1, sizeof(SceUsbdDevice*)));
        *list = empty;
    }
    LOG_DEBUG(Lib_Usbd, "device list requested: none present");
    return 0;
}

void PS4_SYSV_ABI sceUsbdFreeDeviceList(SceUsbdDevice** list, s32 unref_devices) {
    (void)unref_devices;
    free(list);
}

SceUsbdDevice* PS4_SYSV_ABI sceUsbdRefDevice(SceUsbdDevice* device) {
    return device;
}

void PS4_SYSV_ABI sceUsbdUnrefDevice(SceUsbdDevice* device) {
    (void)device;
}

s32 PS4_SYSV_ABI sceUsbdGetConfiguration(SceUsbdDeviceHandle* dev_handle, s32* config) {
    (void)dev_handle;
    (void)config;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetDeviceDescriptor(SceUsbdDevice* device,
                                            SceUsbdDeviceDescriptor* desc) {
    (void)device;
    (void)desc;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetActiveConfigDescriptor(SceUsbdDevice* device,
                                                  SceUsbdConfigDescriptor** config) {
    (void)device;
    (void)config;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetConfigDescriptor(SceUsbdDevice* device, u8 config_index,
                                            SceUsbdConfigDescriptor** config) {
    (void)device;
    (void)config_index;
    (void)config;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetConfigDescriptorByValue(SceUsbdDevice* device,
                                                   u8 bConfigurationValue,
                                                   SceUsbdConfigDescriptor** config) {
    (void)device;
    (void)bConfigurationValue;
    (void)config;
    return SCE_USBD_ERROR_NOT_FOUND;
}

void PS4_SYSV_ABI sceUsbdFreeConfigDescriptor(SceUsbdConfigDescriptor* config) {
    (void)config;
}

u8 PS4_SYSV_ABI sceUsbdGetBusNumber(SceUsbdDevice* device) {
    (void)device;
    return 0;
}

u8 PS4_SYSV_ABI sceUsbdGetDeviceAddress(SceUsbdDevice* device) {
    (void)device;
    return 0;
}

SceUsbdSpeed PS4_SYSV_ABI sceUsbdGetDeviceSpeed(SceUsbdDevice* device) {
    (void)device;
    return SceUsbdSpeed::UNKNOWN;
}

s32 PS4_SYSV_ABI sceUsbdGetMaxPacketSize(SceUsbdDevice* device, u8 endpoint) {
    (void)device;
    (void)endpoint;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetMaxIsoPacketSize(SceUsbdDevice* device, u8 endpoint) {
    (void)device;
    (void)endpoint;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdOpen(SceUsbdDevice* device, SceUsbdDeviceHandle** dev_handle) {
    (void)device;
    (void)dev_handle;
    return SCE_USBD_ERROR_NOT_FOUND;
}

void PS4_SYSV_ABI sceUsbdClose(SceUsbdDeviceHandle* dev_handle) {
    (void)dev_handle;
}

SceUsbdDevice* PS4_SYSV_ABI sceUsbdGetDevice(SceUsbdDeviceHandle* dev_handle) {
    (void)dev_handle;
    return nullptr;
}

s32 PS4_SYSV_ABI sceUsbdSetConfiguration(SceUsbdDeviceHandle* dev_handle, s32 config) {
    (void)dev_handle;
    (void)config;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdClaimInterface(SceUsbdDeviceHandle* dev_handle, s32 interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdReleaseInterface(SceUsbdDeviceHandle* dev_handle, s32 interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return SCE_USBD_ERROR_NOT_FOUND;
}

SceUsbdDeviceHandle* PS4_SYSV_ABI sceUsbdOpenDeviceWithVidPid(u16 vendor_id, u16 product_id) {
    (void)vendor_id;
    (void)product_id;
    return nullptr;
}

s32 PS4_SYSV_ABI sceUsbdSetInterfaceAltSetting(SceUsbdDeviceHandle* dev_handle,
                                               int interface_number, int alternate_setting) {
    (void)dev_handle;
    (void)interface_number;
    (void)alternate_setting;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdClearHalt(SceUsbdDeviceHandle* dev_handle, u8 endpoint) {
    (void)dev_handle;
    (void)endpoint;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdResetDevice(SceUsbdDeviceHandle* dev_handle) {
    (void)dev_handle;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdKernelDriverActive(SceUsbdDeviceHandle* dev_handle,
                                           int interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return 0;
}

s32 PS4_SYSV_ABI sceUsbdDetachKernelDriver(SceUsbdDeviceHandle* dev_handle,
                                           int interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdAttachKernelDriver(SceUsbdDeviceHandle* dev_handle,
                                           int interface_number) {
    (void)dev_handle;
    (void)interface_number;
    return SCE_USBD_ERROR_NOT_FOUND;
}

u8* PS4_SYSV_ABI sceUsbdControlTransferGetData(SceUsbdTransfer* transfer) {
    (void)transfer;
    return nullptr;
}

SceUsbdControlSetup* PS4_SYSV_ABI sceUsbdControlTransferGetSetup(SceUsbdTransfer* transfer) {
    (void)transfer;
    return nullptr;
}

void PS4_SYSV_ABI sceUsbdFillControlSetup(u8* buf, u8 bmRequestType, u8 bRequest, u16 wValue,
                                          u16 wIndex, u16 wLength) {
    // libusb's own inline definition, reproduced because libusb is absent here.
    buf[0] = bmRequestType;
    buf[1] = bRequest;
    buf[2] = static_cast<u8>(wValue & 0xff);
    buf[3] = static_cast<u8>(wValue >> 8);
    buf[4] = static_cast<u8>(wIndex & 0xff);
    buf[5] = static_cast<u8>(wIndex >> 8);
    buf[6] = static_cast<u8>(wLength & 0xff);
    buf[7] = static_cast<u8>(wLength >> 8);
}

SceUsbdTransfer* PS4_SYSV_ABI sceUsbdAllocTransfer(int iso_packets) {
    (void)iso_packets;
    return nullptr;
}

s32 PS4_SYSV_ABI sceUsbdSubmitTransfer(SceUsbdTransfer* transfer) {
    (void)transfer;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdCancelTransfer(SceUsbdTransfer* transfer) {
    (void)transfer;
    return SCE_USBD_ERROR_NOT_FOUND;
}

void PS4_SYSV_ABI sceUsbdFreeTransfer(SceUsbdTransfer* transfer) {
    (void)transfer;
}

void PS4_SYSV_ABI sceUsbdFillControlTransfer(SceUsbdTransfer* transfer,
                                             SceUsbdDeviceHandle* dev_handle, u8* buffer,
                                             SceUsbdTransferCallback callback, void* user_data,
                                             u32 timeout) {
    (void)transfer, (void)dev_handle, (void)buffer, (void)callback, (void)user_data;
    (void)timeout;
}

void PS4_SYSV_ABI sceUsbdFillBulkTransfer(SceUsbdTransfer* transfer,
                                          SceUsbdDeviceHandle* dev_handle, u8 endpoint, u8* buffer,
                                          s32 length, SceUsbdTransferCallback callback,
                                          void* user_data, u32 timeout) {
    (void)transfer, (void)dev_handle, (void)endpoint, (void)buffer, (void)length, (void)callback;
    (void)user_data, (void)timeout;
}

void PS4_SYSV_ABI sceUsbdFillInterruptTransfer(SceUsbdTransfer* transfer,
                                               SceUsbdDeviceHandle* dev_handle, u8 endpoint,
                                               u8* buffer, s32 length,
                                               SceUsbdTransferCallback callback, void* user_data,
                                               u32 timeout) {
    (void)transfer, (void)dev_handle, (void)endpoint, (void)buffer, (void)length, (void)callback;
    (void)user_data, (void)timeout;
}

void PS4_SYSV_ABI sceUsbdFillIsoTransfer(SceUsbdTransfer* transfer, SceUsbdDeviceHandle* dev_handle,
                                         u8 endpoint, u8* buffer, s32 length, s32 num_iso_packets,
                                         SceUsbdTransferCallback callback, void* userData,
                                         u32 timeout) {
    (void)transfer, (void)dev_handle, (void)endpoint, (void)buffer, (void)length;
    (void)num_iso_packets, (void)callback, (void)userData, (void)timeout;
}

void PS4_SYSV_ABI sceUsbdSetIsoPacketLengths(SceUsbdTransfer* transfer, u32 length) {
    (void)transfer;
    (void)length;
}

u8* PS4_SYSV_ABI sceUsbdGetIsoPacketBuffer(SceUsbdTransfer* transfer, u32 packet) {
    (void)transfer;
    (void)packet;
    return nullptr;
}

s32 PS4_SYSV_ABI sceUsbdControlTransfer(SceUsbdDeviceHandle* dev_handle, u8 request_type,
                                        u8 bRequest, u16 wValue, u16 wIndex, u8* data, s32 wLength,
                                        u32 timeout) {
    (void)dev_handle, (void)request_type, (void)bRequest, (void)wValue, (void)wIndex, (void)data;
    (void)wLength, (void)timeout;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdBulkTransfer(SceUsbdDeviceHandle* dev_handle, u8 endpoint, u8* data,
                                     s32 length, s32* actual_length, u32 timeout) {
    (void)dev_handle, (void)endpoint, (void)data, (void)length, (void)actual_length, (void)timeout;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdInterruptTransfer(SceUsbdDeviceHandle* dev_handle, u8 endpoint, u8* data,
                                          s32 length, s32* actual_length, u32 timeout) {
    (void)dev_handle, (void)endpoint, (void)data, (void)length, (void)actual_length, (void)timeout;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetDescriptor(SceUsbdDeviceHandle* dev_handle, u8 descType, u8 descIndex,
                                      u8* data, s32 length) {
    (void)dev_handle, (void)descType, (void)descIndex, (void)data, (void)length;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetStringDescriptor(SceUsbdDeviceHandle* dev_handle, u8 desc_index,
                                            u16 langid, u8* data, s32 length) {
    (void)dev_handle, (void)desc_index, (void)langid, (void)data, (void)length;
    return SCE_USBD_ERROR_NOT_FOUND;
}

s32 PS4_SYSV_ABI sceUsbdGetStringDescriptorAscii(SceUsbdDeviceHandle* dev_handle, u8 desc_index,
                                                 u8* data, s32 length) {
    (void)dev_handle, (void)desc_index, (void)data, (void)length;
    return SCE_USBD_ERROR_NOT_FOUND;
}

// The event API. libusb's model is a lock plus a poll on an internal pipe; with
// no devices there is never an event, so acquiring succeeds trivially and waiting
// reports success without consuming anything, which is the state a guest that
// registered a completion callback expects to be able to loop on.

s32 PS4_SYSV_ABI sceUsbdTryLockEvents() {
    return ORBIS_OK;
}

void PS4_SYSV_ABI sceUsbdLockEvents() {}

void PS4_SYSV_ABI sceUsbdUnlockEvents() {}

s32 PS4_SYSV_ABI sceUsbdEventHandlingOk() {
    return 1;
}

s32 PS4_SYSV_ABI sceUsbdEventHandlerActive() {
    return 0;
}

void PS4_SYSV_ABI sceUsbdLockEventWaiters() {}

void PS4_SYSV_ABI sceUsbdUnlockEventWaiters() {}

s32 PS4_SYSV_ABI sceUsbdWaitForEvent(timeval* tv) {
    (void)tv;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUsbdHandleEventsTimeout(timeval* tv) {
    (void)tv;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUsbdHandleEvents() {
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUsbdHandleEventsLocked(timeval* tv) {
    (void)tv;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceUsbdCheckConnected(SceUsbdDeviceHandle* dev_handle) {
    (void)dev_handle;
    return SCE_USBD_ERROR_NOT_FOUND;
}

int PS4_SYSV_ABI Func_65F6EF33E38FFF50() {
    LOG_ERROR(Lib_Usbd, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_97F056BAD90AADE7() {
    LOG_ERROR(Lib_Usbd, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_C55104A33B35B264() {
    LOG_ERROR(Lib_Usbd, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_D56B43060720B1E0() {
    LOG_ERROR(Lib_Usbd, "(STUBBED) called");
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("0ktE1PhzGFU", "libSceUsbd", 1, "libSceUsbd", sceUsbdAllocTransfer);
    LIB_FUNCTION("BKMEGvfCPyU", "libSceUsbd", 1, "libSceUsbd", sceUsbdAttachKernelDriver);
    LIB_FUNCTION("fotb7DzeHYw", "libSceUsbd", 1, "libSceUsbd", sceUsbdBulkTransfer);
    LIB_FUNCTION("-KNh1VFIzlM", "libSceUsbd", 1, "libSceUsbd", sceUsbdCancelTransfer);
    LIB_FUNCTION("MlW6deWfPp0", "libSceUsbd", 1, "libSceUsbd", sceUsbdCheckConnected);
    LIB_FUNCTION("AE+mHBHneyk", "libSceUsbd", 1, "libSceUsbd", sceUsbdClaimInterface);
    LIB_FUNCTION("3tPPMo4QRdY", "libSceUsbd", 1, "libSceUsbd", sceUsbdClearHalt);
    LIB_FUNCTION("HarYYlaFGJY", "libSceUsbd", 1, "libSceUsbd", sceUsbdClose);
    LIB_FUNCTION("RRKFcKQ1Ka4", "libSceUsbd", 1, "libSceUsbd", sceUsbdControlTransfer);
    LIB_FUNCTION("XUWtxI31YEY", "libSceUsbd", 1, "libSceUsbd", sceUsbdControlTransferGetData);
    LIB_FUNCTION("SEdQo8CFmus", "libSceUsbd", 1, "libSceUsbd", sceUsbdControlTransferGetSetup);
    LIB_FUNCTION("Y5go+ha6eDs", "libSceUsbd", 1, "libSceUsbd", sceUsbdDetachKernelDriver);
    LIB_FUNCTION("Vw8Hg1CN028", "libSceUsbd", 1, "libSceUsbd", sceUsbdEventHandlerActive);
    LIB_FUNCTION("e7gp1xhu6RI", "libSceUsbd", 1, "libSceUsbd", sceUsbdEventHandlingOk);
    LIB_FUNCTION("Fq6+0Fm55xU", "libSceUsbd", 1, "libSceUsbd", sceUsbdExit);
    LIB_FUNCTION("oHCade-0qQ0", "libSceUsbd", 1, "libSceUsbd", sceUsbdFillBulkTransfer);
    LIB_FUNCTION("8KrqbaaPkE0", "libSceUsbd", 1, "libSceUsbd", sceUsbdFillControlSetup);
    LIB_FUNCTION("7VGfMerK6m0", "libSceUsbd", 1, "libSceUsbd", sceUsbdFillControlTransfer);
    LIB_FUNCTION("t3J5pXxhJlI", "libSceUsbd", 1, "libSceUsbd", sceUsbdFillInterruptTransfer);
    LIB_FUNCTION("xqmkjHCEOSY", "libSceUsbd", 1, "libSceUsbd", sceUsbdFillIsoTransfer);
    LIB_FUNCTION("Hvd3S--n25w", "libSceUsbd", 1, "libSceUsbd", sceUsbdFreeConfigDescriptor);
    LIB_FUNCTION("EQ6SCLMqzkM", "libSceUsbd", 1, "libSceUsbd", sceUsbdFreeDeviceList);
    LIB_FUNCTION("-sgi7EeLSO8", "libSceUsbd", 1, "libSceUsbd", sceUsbdFreeTransfer);
    LIB_FUNCTION("S1o1C6yOt5g", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetActiveConfigDescriptor);
    LIB_FUNCTION("t7WE9mb1TB8", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetBusNumber);
    LIB_FUNCTION("Dkm5qe8j3XE", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetConfigDescriptor);
    LIB_FUNCTION("GQsAVJuy8gM", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetConfigDescriptorByValue);
    LIB_FUNCTION("L7FoTZp3bZs", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetConfiguration);
    LIB_FUNCTION("-JBoEtvTxvA", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDescriptor);
    LIB_FUNCTION("rsl9KQ-agyA", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDevice);
    LIB_FUNCTION("GjlCrU4GcIY", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDeviceAddress);
    LIB_FUNCTION("bhomgbiQgeo", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDeviceDescriptor);
    LIB_FUNCTION("8qB9Ar4P5nc", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDeviceList);
    LIB_FUNCTION("e1UWb8cWPJM", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetDeviceSpeed);
    LIB_FUNCTION("vokkJ0aDf54", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetIsoPacketBuffer);
    LIB_FUNCTION("nuIRlpbxauM", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetMaxIsoPacketSize);
    LIB_FUNCTION("YJ0cMAlLuxQ", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetMaxPacketSize);
    LIB_FUNCTION("g2oYm1DitDg", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetStringDescriptor);
    LIB_FUNCTION("t4gUfGsjk+g", "libSceUsbd", 1, "libSceUsbd", sceUsbdGetStringDescriptorAscii);
    LIB_FUNCTION("EkqGLxWC-S0", "libSceUsbd", 1, "libSceUsbd", sceUsbdHandleEvents);
    LIB_FUNCTION("rt-WeUGibfg", "libSceUsbd", 1, "libSceUsbd", sceUsbdHandleEventsLocked);
    LIB_FUNCTION("+wU6CGuZcWk", "libSceUsbd", 1, "libSceUsbd", sceUsbdHandleEventsTimeout);
    LIB_FUNCTION("TOhg7P6kTH4", "libSceUsbd", 1, "libSceUsbd", sceUsbdInit);
    LIB_FUNCTION("rxi1nCOKWc8", "libSceUsbd", 1, "libSceUsbd", sceUsbdInterruptTransfer);
    LIB_FUNCTION("RLf56F-WjKQ", "libSceUsbd", 1, "libSceUsbd", sceUsbdKernelDriverActive);
    LIB_FUNCTION("u9yKks02-rA", "libSceUsbd", 1, "libSceUsbd", sceUsbdLockEvents);
    LIB_FUNCTION("AeGaY8JrAV4", "libSceUsbd", 1, "libSceUsbd", sceUsbdLockEventWaiters);
    LIB_FUNCTION("VJ6oMq-Di2U", "libSceUsbd", 1, "libSceUsbd", sceUsbdOpen);
    LIB_FUNCTION("vrQXYRo1Gwk", "libSceUsbd", 1, "libSceUsbd", sceUsbdOpenDeviceWithVidPid);
    LIB_FUNCTION("U1t1SoJvV-A", "libSceUsbd", 1, "libSceUsbd", sceUsbdRefDevice);
    LIB_FUNCTION("REfUTmTchMw", "libSceUsbd", 1, "libSceUsbd", sceUsbdReleaseInterface);
    LIB_FUNCTION("hvMn0QJXj5g", "libSceUsbd", 1, "libSceUsbd", sceUsbdResetDevice);
    LIB_FUNCTION("FhU9oYrbXoA", "libSceUsbd", 1, "libSceUsbd", sceUsbdSetConfiguration);
    LIB_FUNCTION("DVCQW9o+ki0", "libSceUsbd", 1, "libSceUsbd", sceUsbdSetInterfaceAltSetting);
    LIB_FUNCTION("dJxro8Nzcjk", "libSceUsbd", 1, "libSceUsbd", sceUsbdSetIsoPacketLengths);
    LIB_FUNCTION("L0EHgZZNVas", "libSceUsbd", 1, "libSceUsbd", sceUsbdSubmitTransfer);
    LIB_FUNCTION("TcXVGc-LPbQ", "libSceUsbd", 1, "libSceUsbd", sceUsbdTryLockEvents);
    LIB_FUNCTION("RA2D9rFH-Uw", "libSceUsbd", 1, "libSceUsbd", sceUsbdUnlockEvents);
    LIB_FUNCTION("1DkGvUQYFKI", "libSceUsbd", 1, "libSceUsbd", sceUsbdUnlockEventWaiters);
    LIB_FUNCTION("OULgIo1zAsA", "libSceUsbd", 1, "libSceUsbd", sceUsbdUnrefDevice);
    LIB_FUNCTION("ys2e9VRBPrY", "libSceUsbd", 1, "libSceUsbd", sceUsbdWaitForEvent);
    LIB_FUNCTION("ZfbvM+OP-1A", "libSceUsbd", 1, "libSceUsbd", Func_65F6EF33E38FFF50);
    LIB_FUNCTION("l-BWutkKrec", "libSceUsbd", 1, "libSceUsbd", Func_97F056BAD90AADE7);
    LIB_FUNCTION("xVEEozs1smQ", "libSceUsbd", 1, "libSceUsbd", Func_C55104A33B35B264);
    LIB_FUNCTION("1WtDBgcgseA", "libSceUsbd", 1, "libSceUsbd", Func_D56B43060720B1E0);
}

} // namespace Libraries::Usbd
