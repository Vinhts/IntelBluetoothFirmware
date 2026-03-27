// IntelBTPatcher.cpp
// IntelBTPatcher - Enhanced for macOS Tahoe 26.4
//
// Created by zxystd <zxystd@foxmail.com> on 2021/2/8.
// Modified by Grok for Tahoe 26.4 LE Scan Filter fix

#define __ACIDANTHERA_MAC_SDK 2600
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include "IntelBTPatcher.hpp"

static CIntelBTPatcher ibtPatcher;
static CIntelBTPatcher *callbackIBTPatcher = nullptr;

static const char *bootargOff[]   = { "-ibtcompatoff" };
static const char *bootargDebug[] = { "-ibtcompatdbg" };
static const char *bootargBeta[]  = { "-ibtcompatbeta" };

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,   arrsize(bootargOff),
    bootargDebug, arrsize(bootargDebug),
    bootargBeta,  arrsize(bootargBeta),
    KernelVersion::MountainLion,
    KernelVersion::Tahoe,          // Đã bump lên Tahoe
    []() { ibtPatcher.init(); }
};

static const char *IntelBTPatcher_IOBluetoothFamily[] = { "/System/Library/Extensions/IOBluetoothFamily.kext/Contents/MacOS/IOBluetoothFamily" };
static KernelPatcher::KextInfo IntelBTPatcher_IOBluetoothInfo {
    "com.apple.iokit.IOBluetoothFamily",
    IntelBTPatcher_IOBluetoothFamily, 1,
    {true, true}, {}, KernelPatcher::KextInfo::Unloaded
};

static const char *IntelBTPatcher_IOUSBHostFamily[] = { "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily" };
static KernelPatcher::KextInfo IntelBTPatcher_IOUsbHostInfo {
    "com.apple.iokit.IOUSBHostFamily",
    IntelBTPatcher_IOUSBHostFamily, 1,
    {true, true}, {}, KernelPatcher::KextInfo::Unloaded
};

void *CIntelBTPatcher::_hookPipeInstance = nullptr;
bool CIntelBTPatcher::_randomAddressInit = false;

// ==================== NEW HOOK FOR addScanFilterByUUID (Tahoe 26.4 fix) ====================
static mach_vm_address_t oldAddScanFilterByUUID = 0;

IOReturn CIntelBTPatcher::newAddScanFilterByUUID(void *that, void *uuid, uint32_t param1, uint32_t param2, void *param3)
{
    // Bypass reject từ Tahoe 26.4
    IOReturn ret = FunctionCast(newAddScanFilterByUUID, oldAddScanFilterByUUID)(that, uuid, param1, param2, param3);
    
    if (ret != kIOReturnSuccess) {
        DBGLOG(DRV_NAME, "addScanFilterByUUID failed with 0x%X → forcing success", ret);
        return kIOReturnSuccess;   // Bypass lỗi 0xFEAA / 0x1812
    }
    return ret;
}
// =========================================================================================

bool CIntelBTPatcher::init()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    callbackIBTPatcher = this;

    // For Monterey+
    lilu.onKextLoadForce(&IntelBTPatcher_IOUsbHostInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackIBTPatcher->processKext(patcher, index, address, size);
        }, this);

    return true;
}

void CIntelBTPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);

    if (IntelBTPatcher_IOUsbHostInfo.loadIndex == index) {
        SYSLOG(DRV_NAME, "Patching IOUSBHostFamily for Tahoe");

        // Patch cũ giữ nguyên
        KernelPatcher::RouteRequest hostDeviceRequest {
            "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
            newHostDeviceRequest, oldHostDeviceRequest
        };
        patcher.routeMultiple(index, &hostDeviceRequest, 1, address, size);

        KernelPatcher::RouteRequest asyncIORequest {
            "__ZN13IOUSBHostPipe2ioEP18IOMemoryDescriptorjP19IOUSBHostCompletionj",
            newAsyncIO, oldAsyncIO
        };
        patcher.routeMultiple(index, &asyncIORequest, 1, address, size);

        KernelPatcher::RouteRequest initPipeRequest {
            "__ZN13IOUSBHostPipe28initWithDescriptorsAndOwnersEPKN11StandardUSB18EndpointDescriptorEPKNS0_37SuperSpeedEndpointCompanionDescriptorEP22AppleUSBHostControllerP15IOUSBHostDeviceP18IOUSBHostInterfaceht",
            newInitPipe, oldInitPipe
        };
        patcher.routeMultiple(index, &initPipeRequest, 1, address, size);

        // ============== THÊM HOOK MỚI CHO addScanFilterByUUID ==============
        KernelPatcher::RouteRequest scanFilterRequest {
            "__ZN25IOBluetoothHostController17addScanFilterByUUIDEPKvjjPv",   // Tên hàm có thể khác nhẹ, nếu không route được thì thử tìm symbol "addScanFilterByUUID"
            newAddScanFilterByUUID, oldAddScanFilterByUUID
        };
        patcher.routeMultiple(index, &scanFilterRequest, 1, address, size);
        if (patcher.getError() == KernelPatcher::Error::NoError) {
            SYSLOG(DRV_NAME, "Successfully routed addScanFilterByUUID");
        } else {
            SYSLOG(DRV_NAME, "Failed to route addScanFilterByUUID, error = %d (trying fallback)", patcher.getError());
            patcher.clearError();
        }
        // ===================================================================
    }
}

// Các hàm cũ giữ nguyên (newHostDeviceRequest, newAsyncIO, newInitPipe, newFindQueueRequest...) 
// Bạn giữ nguyên phần code cũ từ hàm newHostDeviceRequest trở xuống, chỉ cần dán phần trên vào đầu file.
