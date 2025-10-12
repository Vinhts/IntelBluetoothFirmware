#pragma once

#include <Headers/kern_util.hpp>
#include <Headers/kern_api.hpp>

typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t len;
} HciEventHdr;

typedef struct __attribute__((packed)) {
    HciEventHdr evt;
    uint8_t numCommands;
    uint16_t opcode;
    uint8_t data[];
} HciCommand;

typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t len;
    uint8_t data[];
} HciEvent;

typedef struct AsyncOwnerData {
    void (*action)(void *, void*, IOReturn, uint32_t);
    void *owner;
    IOMemoryDescriptor *dataBuffer;
} AsyncOwnerData;

class CIntelBTPatcher {
public:
    static bool init();
    static IOReturn newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout);
    static IOReturn newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t dataBufferLength, IOUSBHostCompletion* completion, uint32_t completionTimeoutMs);
    static int newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor, StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor, AppleUSBHostController *controller, IOUSBHostDevice *device, IOUSBHostInterface *interface, unsigned char, unsigned short);

    // placeholders for original saved symbols (names may differ in upstream)
    static mach_vm_address_t oldFindQueueRequest;
    static mach_vm_address_t oldHostDeviceRequest;
    static mach_vm_address_t oldAsyncIO;
    static mach_vm_address_t oldInitPipe;

private:
    static void *_hookPipeInstance;
    static AsyncOwnerData *_interruptPipeAsyncOwner;
    static bool _randomAddressInit;
};
