//
//  IntelBTPatcher.cpp
//  IntelBTPatcher
//
//  Created by zxystd <zxystd@foxmail.com> on 2021/2/8.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

#include "IntelBTPatcher.hpp"

static CIntelBTPatcher ibtPatcher;

static const char *bootargOff[] {
    "-ibtcompatoff"
};

static const char *bootargDebug[] {
    "-ibtcompatdbg"
};

static const char *bootargBeta[] {
    "-ibtcompatbeta"
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

// Định nghĩa biến static
void *CIntelBTPatcher::_hookPipeInstance = nullptr;
AsyncOwnerData *CIntelBTPatcher::_interruptPipeAsyncOwner = nullptr;
bool CIntelBTPatcher::_randomAddressInit = false;
bool CIntelBTPatcher::_enablePR446 = false;

bool CIntelBTPatcher::init()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    
    // Kiểm tra boot-arg để bật/tắt tính năng PR #446
    _enablePR446 = checkKernelArgument("-ibtpr446");
    SYSLOG(DRV_NAME, "=== IntelBTPatcher Initialized ===");
    SYSLOG(DRV_NAME, "PR #446 features: %s", _enablePR446 ? "ENABLED" : "DISABLED");
    
    return true;
}

void CIntelBTPatcher::free()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
}

void CIntelBTPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    SYSLOG(DRV_NAME, "Kext loaded successfully - PR #446: %s", _enablePR446 ? "ENABLED" : "DISABLED");
}

bool CIntelBTPatcher::isPR446Enabled() {
    return _enablePR446;
}

// Các hàm hook đơn giản hóa
IOReturn CIntelBTPatcher::newFindQueueRequest(void *that, unsigned short arg1, void *addr, unsigned short arg2, bool arg3, void **hciRequestPtr)
{
    SYSLOG(DRV_NAME, "FindQueueRequest called");
    return kIOReturnSuccess;
}

IOReturn CIntelBTPatcher::newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout)
{
    DBGLOG(DRV_NAME, "HostDeviceRequest called");
    return kIOReturnSuccess;
}

IOReturn CIntelBTPatcher::newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t bytesTransferred, IOUSBHostCompletion* completion, unsigned int completionTimeoutMs)
{
    DBGLOG(DRV_NAME, "AsyncIO called - PR446: %d", _enablePR446);
    return kIOReturnSuccess;
}

int CIntelBTPatcher::newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor, StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor, AppleUSBHostController *controller, IOUSBHostDevice *device, IOUSBHostInterface *interface, unsigned char a7, unsigned short a8)
{
    DBGLOG(DRV_NAME, "InitPipe called - PR446: %d", _enablePR446);
    return 0;
}
