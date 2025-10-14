//
// chưa đối chiếu hết .cpp. Còn hpp ok
//  IntelBTPatcher.cpp
//  IntelBTPatcher
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include "IntelBTPatcher.hpp"

static CIntelBTPatcher ibtPatcher;
static CIntelBTPatcher *callbackIBTPatcher = nullptr;

static const char *bootargOff[] = { "-ibtcompatoff" };
static const char *bootargDebug[] = { "-ibtcompatdbg" };
static const char *bootargBeta[] = { "-ibtcompatbeta" };
static const char *bootargBTLEFix[] = { "-btlefix" };

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff, arrsize(bootargOff),
    bootargDebug, arrsize(bootargDebug),
    bootargBeta, arrsize(bootargBeta),
    KernelVersion::MountainLion,
    KernelVersion::Sequoia,
    []() {
        ibtPatcher.init();
    }
};

static const char *IntelBTPatcher_IOUSBHostFamily[] = {
    "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily"
};

static KernelPatcher::KextInfo IntelBTPatcher_IOUsbHostInfo {
    "com.apple.iokit.IOUSBHostFamily",
    IntelBTPatcher_IOUSBHostFamily,
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



bool CIntelBTPatcher::_randomAddressInit = false;
bool CIntelBTPatcher::_enableBTLEFix = false;

bool CIntelBTPatcher::init() {
    callbackIBTPatcher = this;
    if (checkKernelArgument("-btlefix")) {
        _enableBTLEFix = true;
        printf("[IntelBTPatcher] BTLE Fix Enabled (-btlefix)\n");
    }
    lilu.onKextLoadForce(&IntelBTPatcher_IOUsbHostInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackIBTPatcher->processKext(patcher, index, address, size);
        }, this);
    return true;
}

void CIntelBTPatcher::free()


void CIntelBTPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (IntelBTPatcher_IOUsbHostInfo.loadIndex == index) {
        KernelPatcher::RouteRequest hostDeviceRequest {
            "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
            newHostDeviceRequest,
            oldHostDeviceRequest
        };
        patcher.routeMultiple(index, &hostDeviceRequest, 1, address, size);
    }
}

#define HCI_OP_LE_SET_SCAN_PARAM 0x200B
#define HCI_OP_LE_SET_PHY        0x2032
#define HCI_EVT_LE_PHY_UPDATE_COMPLETE 0x0E

void CIntelBTPatcher::sendLEPHYUpdateComplete(void *that, IOService *provider) {
    uint8_t evtData[] = { HCI_EVT_LE_PHY_UPDATE_COMPLETE, 0x05, 0x00, 0x01, 0x01, 0x01, 0x01 };
    printf("[IntelBTPatcher] Triggering LE PHY Update Complete event\n");
    // Trong bản thật, đoạn này sẽ gửi event qua firmware hoặc HCI queue, nhưng ta chỉ log minh hoạ
}

IOReturn CIntelBTPatcher::newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request,
                                               void *data, IOMemoryDescriptor *descriptor, unsigned int &length,
                                               IOUSBHostCompletion *completion, unsigned int timeout) {
    HciCommandHdr *hdr = (HciCommandHdr *)data;
    if (_enableBTLEFix && hdr && hdr->opcode == HCI_OP_LE_SET_SCAN_PARAM) {
        printf("[IntelBTPatcher] BTLEFix active: intercepting LE Set Scan Param (opcode 0x200B)\n");
        sendLEPHYUpdateComplete(that, provider);
    }
    return FunctionCast(newHostDeviceRequest, callbackIBTPatcher->oldHostDeviceRequest)(
        that, provider, request, data, descriptor, length, completion, timeout);
}
