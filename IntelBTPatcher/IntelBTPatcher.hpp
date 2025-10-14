//
//  IntelBTPatcher.h
//  IntelBTPatcher
//
//  Created by zxystd <zxystd@foxmail.com> on 2021/2/8.
//

#ifndef IntelBTPatcher_h
#define IntelBTPatcher_h

#include <Headers/kern_patcher.hpp>
#include <IOKit/usb/IOUSBHostDevice.h>

#define DRV_NAME "ibtp"

class BluetoothDeviceAddress;

typedef struct {
    void *owner;
    IOMemoryDescriptor *dataBuffer;
    IOUSBHostCompletionAction action;
} AsyncOwnerData;

typedef struct __attribute__((packed))
{
    uint16_t    opcode;
    uint8_t     len;
    uint8_t     data[];
} HciCommandHdr;

typedef struct __attribute__((packed))
{
    uint8_t     evt;
    uint8_t     len;
} HciEventHdr;

typedef struct __attribute__((packed)) 
{
    HciEventHdr evt;
    uint8_t     numCommands;
    uint16_t    opcode;
    uint8_t     data[];
} HciResponse;

const char* _hexDumpHCIData(uint8_t *buf, size_t len);

class CIntelBTPatcher {
public:
    bool init();
    void free();
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    
    static IOReturn newFindQueueRequest(void *that, unsigned short arg1, void *addr, unsigned short arg2, bool arg3, void **hciRequestPtr);
    static IOReturn newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout);

    mach_vm_address_t oldFindQueueRequest {};
    mach_vm_address_t oldHostDeviceRequest {};

private:
    static bool _randomAddressInit;
    static bool _enableBTLEFix;
    static void sendLEPHYUpdateComplete(void *that, IOService *provider);
};

#endif /* IntelBTPatcher_h */
