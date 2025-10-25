//
//  IntelBTPatcher.cpp
//  IntelBTPatcher
//
//  Created by lshbluesky on …
//  Copyright © 2025 lshbluesky. All rights reserved.
//

#include "IntelBTPatcher.hpp"
#include <Headers/kern_util.hpp>
#include <IOKit/usb/IOUSBHost.h>

#define DRV_NAME "IntelBTPatcher"
#define VENDOR_USB_INTEL 0x8087

// ---------- STATIC GLOBALS ----------
CIntelBTPatcher *CIntelBTPatcher::callbackIBTPatcher = nullptr;
void *CIntelBTPatcher::_hookPipeInstance = nullptr;
CIntelBTPatcher::AsyncOwnerData *CIntelBTPatcher::_interruptPipeAsyncOwner = nullptr;
bool CIntelBTPatcher::_randomAddressInit = false;

// ---------- FAKE PHY EVENT ----------
static uint8_t fakePhyUpdateCompleteEvent[8] = {
    0x3E, 0x06, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x02   // LE PHY Update Complete (2 Mbps)
};

#define HCI_EVT_LE_META                     0x3E
#define HCI_EVT_LE_META_READ_REMOTE_FEATURES_COMPLETE 0x04
#define HCI_OP_LE_READ_REMOTE_FEATURES      0x2016

//===================================================================
//  INIT / DEINIT
//===================================================================
bool CIntelBTPatcher::init() {
    callbackIBTPatcher = this;
    return true;
}

void CIntelBTPatcher::deinit() {
    if (_interruptPipeAsyncOwner) { delete _interruptPipeAsyncOwner; }
    callbackIBTPatcher = nullptr;
}

//===================================================================
//  PROCESS KEXT
//===================================================================
void CIntelBTPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (!address || !size) return;

    // ---- ORIGINAL ROUTE (hostDeviceRequest) ----
    KernelPatcher::RouteRequest hostRequest {
        "__ZN18IOUSBHostInterface15hostDeviceRequestEP17IOUSBHostIORequestP19IOUSBHostCompletionP22IOUSBDescriptorHeaderjj",
        wrapHostDeviceRequest,
        oldHostDeviceRequest
    };
    patcher.routeMultiple(index, &hostRequest, 1, address, size);
    if (patcher.getError() == KernelPatcher::Error::NoError)
        SYSLOG(DRV_NAME, "routed hostDeviceRequest");
    else
        patcher.clearError();

    // ---- FAKE PHY: asyncIO ----
    KernelPatcher::RouteRequest asyncIORequest {
        "__ZN13IOUSBHostPipe2ioEP18IOMemoryDescriptorjP19IOUSBHostCompletionj",
        newAsyncIO,
        oldAsyncIO
    };
    patcher.routeMultiple(index, &asyncIORequest, 1, address, size);
    if (patcher.getError() == KernelPatcher::Error::NoError)
        SYSLOG(DRV_NAME, "routed asyncIO for fake PHY");
    else
        patcher.clearError();

    // ---- FAKE PHY: initPipe ----
    KernelPatcher::RouteRequest initPipeRequest {
        "__ZN13IOUSBHostPipe28initWithDescriptorsAndOwnersEPKN11StandardUSB18EndpointDescriptorEPKNS0_37SuperSpeedEndpointCompanionDescriptorEP22AppleUSBHostControllerP15IOUSBHostDeviceP18IOUSBHostInterfaceht",
        newInitPipe,
        oldInitPipe
    };
    patcher.routeMultiple(index, &initPipeRequest, 1, address, size);
    if (patcher.getError() == KernelPatcher::Error::NoError)
        SYSLOG(DRV_NAME, "routed initPipe for fake PHY");
    else
        patcher.clearError();
}

//===================================================================
//  ORIGINAL WRAPPER (hostDeviceRequest)
//===================================================================
IOReturn CIntelBTPatcher::wrapHostDeviceRequest(void *that, IOService *provider,
                                                IOUSBHostIORequest *request,
                                                IOUSBHostCompletion *completion,
                                                IOUSBDescriptorHeader *descriptor,
                                                uint32_t length,
                                                uint32_t timeout) {
    auto *hdr = reinterpret_cast<HciCommandHdr *>(request->getDataBuffer()->getBytesNoCopy());
    if (hdr && hdr->opcode == HCI_OP_LE_READ_REMOTE_FEATURES) {
        SYSLOG(DRV_NAME, "[PATCH] Injected extra LE Read Remote Features (Secure Connections)");
    }
    return FunctionCast(wrapHostDeviceRequest, callbackIBTPatcher->oldHostDeviceRequest)(
        that, provider, request, completion, descriptor, length, timeout);
}

//===================================================================
//  FAKE PHY: initPipe
//===================================================================
int CIntelBTPatcher::newInitPipe(void *that,
                                 StandardUSB::EndpointDescriptor const *descriptor,
                                 StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor,
                                 AppleUSBHostController *controller,
                                 IOUSBHostDevice *device,
                                 IOUSBHostInterface *interface,
                                 unsigned char a7,
                                 unsigned short a8) {
    int ret = FunctionCast(newInitPipe, callbackIBTPatcher->oldInitPipe)(
        that, descriptor, superDescriptor, controller, device, interface, a7, a8);

    if (device && device->getDeviceDescriptor()->idVendor == VENDOR_USB_INTEL) {
        if (StandardUSB::getEndpointType(descriptor) == kIOUSBEndpointTypeInterrupt) {
            _hookPipeInstance = that;
            if (!_interruptPipeAsyncOwner)
                _interruptPipeAsyncOwner = new AsyncOwnerData;
            _randomAddressInit = false;
            SYSLOG(DRV_NAME, "[PATCH] Hooked Intel BT interrupt pipe – ready for fake PHY");
        }
    }
    return ret;
}

//===================================================================
//  FAKE PHY: asyncIO
//===================================================================
IOReturn CIntelBTPatcher::newAsyncIO(void *that,
                                     IOMemoryDescriptor* dataBuffer,
                                     uint32_t bytesTransferred,
                                     IOUSBHostCompletion* completion,
                                     uint32_t completionTimeoutMs) {
    if (that == _hookPipeInstance && completion) {
        _interruptPipeAsyncOwner->action   = completion->action;
        _interruptPipeAsyncOwner->owner    = completion->owner;
        _interruptPipeAsyncOwner->dataBuffer = dataBuffer;

        completion->action = asyncIOCompletion;
        completion->owner  = _interruptPipeAsyncOwner;
    }
    return FunctionCast(newAsyncIO, callbackIBTPatcher->oldAsyncIO)(
        that, dataBuffer, bytesTransferred, completion, completionTimeoutMs);
}

//===================================================================
//  FAKE PHY: completion handler
//===================================================================
void CIntelBTPatcher::asyncIOCompletion(void* owner, void* parameter, IOReturn status, uint32_t bytesTransferred) {
    AsyncOwnerData *asyncOwner = static_cast<AsyncOwnerData *>(owner);
    IOMemoryDescriptor* dataBuffer = asyncOwner->dataBuffer;
    static bool skipExtraReadRemoteFeaturesComplete = true;

    if (dataBuffer && bytesTransferred > 0) {
        void *buf = IOMalloc(bytesTransferred);
        if (buf) {
            dataBuffer->readBytes(0, buf, bytesTransferred);
            HciEventHdr *hdr = static_cast<HciEventHdr *>(buf);

            if (hdr->evt == HCI_EVT_LE_META &&
                hdr->len >= 3 &&
                hdr->data[0] == HCI_EVT_LE_META_READ_REMOTE_FEATURES_COMPLETE) {

                if (skipExtraReadRemoteFeaturesComplete) {
                    skipExtraReadRemoteFeaturesComplete = false;
                } else {
                    // ---- INJECT FAKE LE PHY UPDATE COMPLETE ----
                    fakePhyUpdateCompleteEvent[4] = hdr->data[2]; // low byte handle
                    fakePhyUpdateCompleteEvent[5] = hdr->data[3]; // high byte handle
                    dataBuffer->writeBytes(0, fakePhyUpdateCompleteEvent, sizeof(fakePhyUpdateCompleteEvent));

                    skipExtraReadRemoteFeaturesComplete = true;
                    SYSLOG(DRV_NAME, "[PATCH] Injected fake LE PHY Update Complete (handle 0x%02X%02X)",
                           hdr->data[3], hdr->data[2]);
                }
            }
            IOFree(buf, bytesTransferred);
        }
    }

    if (asyncOwner->action)
        asyncOwner->action(asyncOwner->owner, parameter, status, bytesTransferred);
}

//===================================================================
//  PLUGIN ENTRY
//===================================================================
static CIntelBTPatcher ibtPatcher;

static KernelPatcher::KextInfo kextList[] = {
    { "com.intel.wifi", nullptr, 1, {}, KernelPatcher::KextInfo::Unloaded }
};

extern "C" void pluginStart(KernelPatcher &patcher) {
    lilu.onKextLoadForce(kextList, arrsize(kextList),
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            static_cast<CIntelBTPatcher*>(user)->processKext(patcher, index, address, size);
        }, &ibtPatcher);
}
