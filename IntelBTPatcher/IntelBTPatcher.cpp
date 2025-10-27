//
//  IntelBTPatcher.cpp
//  IntelBTPatcher
//
//  Created by zxystd <zxystd@foxmail.com> on 2021/2/8.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include <IOKit/IOLib.h>

#include "IntelBTPatcher.hpp"

static CIntelBTPatcher ibtPatcher;
static CIntelBTPatcher *callbackIBTPatcher = nullptr;

static const char *bootargOff[] {
    "-ibtcompatoff"
};

static const char *bootargDebug[] {
    "-ibtcompatdbg"
};

static const char *bootargBeta[] {
    "-ibtcompatbeta"
};

// Boot-arg mới để bật/tắt tính năng PR #446
static const char *bootargPR446[] {
    "-ibtpr446"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::MountainLion,
    KernelVersion::Sequoia,
    []() {
        ibtPatcher.init();
    }
};

static const char *IntelBTPatcher_IOBluetoothFamily[] { "/System/Library/Extensions/IOBluetoothFamily.kext/Contents/MacOS/IOBluetoothFamily" };

static KernelPatcher::KextInfo IntelBTPatcher_IOBluetoothInfo {
    "com.apple.iokit.IOBluetoothFamily",
    IntelBTPatcher_IOBluetoothFamily,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

static const char *IntelBTPatcher_IOUSBHostFamily[] {
    "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily" };

static KernelPatcher::KextInfo IntelBTPatcher_IOUsbHostInfo {
    "com.apple.iokit.IOUSBHostFamily",
    IntelBTPatcher_IOUSBHostFamily,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

// Định nghĩa biến static
void *CIntelBTPatcher::_hookPipeInstance = nullptr;
AsyncOwnerData *CIntelBTPatcher::_interruptPipeAsyncOwner = nullptr;
bool CIntelBTPatcher::_randomAddressInit = false;
bool CIntelBTPatcher::_enablePR446 = false;

bool CIntelBTPatcher::init()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    callbackIBTPatcher = this;
    
    // Kiểm tra boot-arg để bật/tắt tính năng PR #446
    _enablePR446 = checkKernelArgument(bootargPR446[0]);
    DBGLOG(DRV_NAME, "PR #446 features %s", _enablePR446 ? "enabled" : "disabled");
    
    // LOG QUAN TRỌNG: Ghi lại trạng thái
    SYSLOG(DRV_NAME, "=== IntelBTPatcher Initialized ===");
    SYSLOG(DRV_NAME, "PR #446 features: %s", _enablePR446 ? "ENABLED" : "DISABLED");
    SYSLOG(DRV_NAME, "Kernel version: %d", getKernelVersion());
    
    if (getKernelVersion() < KernelVersion::Monterey) {
        lilu.onKextLoadForce(&IntelBTPatcher_IOBluetoothInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackIBTPatcher->processKext(patcher, index, address, size);
        }, this);
    } else {
        lilu.onKextLoadForce(&IntelBTPatcher_IOUsbHostInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackIBTPatcher->processKext(patcher, index, address, size);
        }, this);
    }
    return true;
}

void CIntelBTPatcher::free()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
}

void CIntelBTPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    
    if (getKernelVersion() < KernelVersion::Monterey) {
        if (IntelBTPatcher_IOBluetoothInfo.loadIndex == index) {
            DBGLOG(DRV_NAME, "Loading IOBluetoothFamily (Pre-Monterey)");
            
            KernelPatcher::RouteRequest findQueueRequestRequest {
                "__ZN25IOBluetoothHostController17FindQueuedRequestEtP22BluetoothDeviceAddresstbPP21IOBluetoothHCIRequest",
                newFindQueueRequest,
                oldFindQueueRequest
            };
            
            if (patcher.routeMultiple(index, &findQueueRequestRequest, 1, address, size)) {
                DBGLOG(DRV_NAME, "Successfully routed FindQueuedRequest");
            } else {
                SYSLOG(DRV_NAME, "Failed to route FindQueuedRequest");
            }
        }
    } else {
        if (IntelBTPatcher_IOUsbHostInfo.loadIndex == index) {
            SYSLOG(DRV_NAME, "Loading IOUSBHostFamily (Monterey+)");
            
            // LUÔN route hostDeviceRequest (an toàn)
            KernelPatcher::RouteRequest hostDeviceRequest {
                "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
                newHostDeviceRequest,
                oldHostDeviceRequest
            };
            
            if (patcher.routeMultiple(index, &hostDeviceRequest, 1, address, size)) {
                SYSLOG(DRV_NAME, "Successfully routed hostDeviceRequest");
            } else {
                SYSLOG(DRV_NAME, "Failed to route hostDeviceRequest");
            }

            // CHỈ route PR #446 features nếu được bật EXPLICITLY
            if (_enablePR446) {
                SYSLOG(DRV_NAME, "Routing PR #446 features (ENABLED via boot-arg)");
                
                KernelPatcher::RouteRequest asyncIORequest {
                    "__ZN13IOUSBHostPipe2ioEP18IOMemoryDescriptorjP19IOUSBHostCompletionj",
                    newAsyncIO,
                    oldAsyncIO
                };
                
                KernelPatcher::RouteRequest initPipeRequest {
                    "__ZN13IOUSBHostPipe28initWithDescriptorsAndOwnersEPKN11StandardUSB18EndpointDescriptorEPKNS0_37SuperSpeedEndpointCompanionDescriptorEP22AppleUSBHostControllerP15IOUSBHostDeviceP18IOUSBHostInterfaceht",
                    newInitPipe,
                    oldInitPipe
                };
                
                // Route từng cái một để dễ debug
                if (patcher.routeMultiple(index, &asyncIORequest, 1, address, size)) {
                    SYSLOG(DRV_NAME, "Successfully routed asyncIO");
                } else {
                    SYSLOG(DRV_NAME, "Failed to route asyncIO");
                }
                
                if (patcher.routeMultiple(index, &initPipeRequest, 1, address, size)) {
                    SYSLOG(DRV_NAME, "Successfully routed initPipe");
                } else {
                    SYSLOG(DRV_NAME, "Failed to route initPipe");
                }
            } else {
                SYSLOG(DRV_NAME, "PR #446 features DISABLED (no boot-arg)");
            }
        }
    }
}

#pragma mark - For Bigsur-, patch unhandled 0x2019 opcode

#define HCI_OP_LE_START_ENCRYPTION 0x2019

IOReturn CIntelBTPatcher::newFindQueueRequest(void *that, unsigned short arg1, void *addr, unsigned short arg2, bool arg3, void **hciRequestPtr)
{
    IOReturn ret = FunctionCast(newFindQueueRequest, callbackIBTPatcher->oldFindQueueRequest)(that, arg1, addr, arg2, arg3, hciRequestPtr);
    if (ret != 0 && arg1 == HCI_OP_LE_START_ENCRYPTION) {
        ret = FunctionCast(newFindQueueRequest, callbackIBTPatcher->oldFindQueueRequest)(that, arg1, addr, 0xffff, arg3, hciRequestPtr);
        DBGLOG(DRV_NAME, "%s ret: %d arg1: 0x%04x arg2: 0x%04x arg3: %d ptr: %p", __FUNCTION__, ret, arg1, arg2, arg3, *hciRequestPtr);
    }
    return ret;
}

#pragma mark - For Monterey+ patch for intercept HCI REQ and RESP

IOReturn CIntelBTPatcher::newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout)
{
    // SAFE VERSION: Chỉ log, không can thiệp
    DBGLOG(DRV_NAME, "HostDeviceRequest: bRequest=0x%x, wLength=%u", request.bRequest, request.wLength);
    
    return FunctionCast(newHostDeviceRequest, callbackIBTPatcher->oldHostDeviceRequest)(that, provider, request, data, descriptor, length, completion, timeout);
}

IOReturn CIntelBTPatcher::newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t bytesTransferred, IOUSBHostCompletion* completion, unsigned int completionTimeoutMs)
{
    // SAFE VERSION: Chỉ log khi PR #446 enabled
    if (_enablePR446) {
        DBGLOG(DRV_NAME, "AsyncIO: buffer=%p, bytes=%u", dataBuffer, bytesTransferred);
    }
    
    return FunctionCast(newAsyncIO, callbackIBTPatcher->oldAsyncIO)(that, dataBuffer, bytesTransferred, completion, completionTimeoutMs);
}

int CIntelBTPatcher::newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor, StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor, AppleUSBHostController *controller, IOUSBHostDevice *device, IOUSBHostInterface *interface, unsigned char a7, unsigned short a8)
{
    // SAFE VERSION: Chỉ log khi PR #446 enabled
    if (_enablePR446 && device) {
        DBGLOG(DRV_NAME, "InitPipe: device=%s", device->getName());
    }
    
    return FunctionCast(newInitPipe, callbackIBTPatcher->oldInitPipe)(that, descriptor, superDescriptor, controller, device, interface, a7, a8);
}

bool CIntelBTPatcher::isPR446Enabled() {
    return _enablePR446;
}
