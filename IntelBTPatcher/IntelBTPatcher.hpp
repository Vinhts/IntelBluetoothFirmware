//
//  IntelBTPatcher.hpp
//  IntelBTPatcher
//
//  Created by lshbluesky on …
//  Copyright © 2025 lshbluesky. All rights reserved.
//

#ifndef IntelBTPatcher_hpp
#define IntelBTPatcher_hpp

#include <Headers/kern_patcher.hpp>
#include <Headers/kern_api.hpp>
#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBHostPipe.h>
#include <IOKit/usb/StandardUSB.h>

class CIntelBTPatcher {
public:
    bool init();
    void deinit();

private:
    static CIntelBTPatcher *callbackIBTPatcher;

    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

    // ---------- ORIGINAL ROUTES ----------
    mach_vm_address_t oldHostDeviceRequest {};
    mach_vm_address_t oldAsyncIO {};
    mach_vm_address_t oldInitPipe {};

    // ---------- FAKE PHY FIX (PR #446) ----------
    struct AsyncOwnerData {
        void* owner;
        IOUSBHostCompletion* action;
        IOMemoryDescriptor* dataBuffer;
    };

    static void* _hookPipeInstance;
    static AsyncOwnerData* _interruptPipeAsyncOwner;
    static bool _randomAddressInit;

    static IOReturn newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t bytesTransferred,
                               IOUSBHostCompletion* completion, uint32_t completionTimeoutMs);
    static int newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor,
                           StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor,
                           AppleUSBHostController *controller, IOUSBHostDevice *device,
                           IOUSBHostInterface *interface, unsigned char a7, unsigned short a8);
    static void asyncIOCompletion(void* owner, void* parameter, IOReturn status, uint32_t bytesTransferred);

    // ---------- ORIGINAL FUNCTIONS ----------
    static IOReturn wrapHostDeviceRequest(void *that, IOService *provider,
                                          IOUSBHostIORequest *request,
                                          IOUSBHostCompletion *completion,
                                          IOUSBDescriptorHeader *descriptor,
                                          uint32_t length,
                                          uint32_t timeout);
};

#endif /* IntelBTPatcher_hpp */
