#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

#include "IntelBTPatcher.hpp"

#define DRV_NAME "IntelBTPatcher"
#define VENDOR_USB_INTEL 0x8087

// Boot-arg key: intelbt_lefix=1 to enable LE fix
static bool gLEFixEnabled = false;

// HCI / event defines used by the LE fix logic
#define MAX_HCI_BUF_LEN 255
#define HCI_OP_RESET 0x0c03
#define HCI_OP_LE_SET_SCAN_PARAM 0x200B
#define HCI_OP_LE_SET_SCAN_ENABLE 0x200C
#define HCI_OP_LE_READ_REMOTE_FEATURES 0x2016

#define HCI_EVT_LE_META 0x3E
#define HCI_EVT_LE_META_READ_REMOTE_FEATURES_COMPLETE 0x04

// Fake PHY Update Complete Event buffer (8 bytes) — bytes 4/5 will hold connection handle copied later
static uint8_t fakePhyUpdateCompleteEvent[8] = {0x3E, 0x06, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x02};

using namespace KernelPatcher;

// Static members
void *CIntelBTPatcher::_hookPipeInstance = nullptr;
AsyncOwnerData *CIntelBTPatcher::_interruptPipeAsyncOwner = nullptr;
bool CIntelBTPatcher::_randomAddressInit = false;

// PE_parse_boot_argn from kernel to read boot args
extern "C" {
    int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
}

/**
 * asyncIOCompletion
 *
 * This function wraps the original async completion to inspect HCI event buffers.
 * The logic mirrors the PR#446 approach: when a LE Read Remote Features complete
 * event is observed, copy the connection handle into a crafted PHY Update Complete
 * fake event written back to the original data buffer — macOS expects such event
 * to proceed with LE devices.
 *
 * skipExtraReadRemoteFeaturesComplete toggles to ensure we pair the right events.
 */
static void asyncIOCompletion(void* owner, void* parameter, IOReturn status, uint32_t bytesTransferred)
{
    AsyncOwnerData *asyncOwner = (AsyncOwnerData *)owner;
    if (!asyncOwner)
        return;

    IOMemoryDescriptor* dataBuffer = asyncOwner->dataBuffer;
    static bool skipExtraReadRemoteFeaturesComplete = true;

    if (dataBuffer && bytesTransferred) {
        void *buffer = IOMalloc(bytesTransferred);
        if (buffer) {
            dataBuffer->readBytes(0, buffer, bytesTransferred);

            // We expect HCI event structure: HciEventHdr { evt, len } followed by data[]
            HciEventHdr *hdr = (HciEventHdr *)buffer;
            if (hdr && hdr->evt == HCI_EVT_LE_META && hdr->len > 0) {
                // LE Meta Event layout: data[0] = subevent
                uint8_t sub = ((uint8_t*)buffer)[2]; // careful offset: evt(1), len(1), data[0] at offset 2
                if (sub == HCI_EVT_LE_META_READ_REMOTE_FEATURES_COMPLETE) {
                    if (skipExtraReadRemoteFeaturesComplete) {
                        // first time: flip flag and skip (PR approach)
                        skipExtraReadRemoteFeaturesComplete = false;
                    } else {
                        // second time: read connection handle from this event and craft fake PHY update event
                        // Here we assume hdr->data[...] layout: subevent specific bytes; connection handle at known offsets (data[2],data[3])
                        uint8_t *dataStart = ((uint8_t*)buffer) + sizeof(HciEventHdr);
                        // sanity check len
                        if (hdr->len >= 4) {
                            // offset within dataStart: dataStart[2..3] == connection handle (little-endian)
                            fakePhyUpdateCompleteEvent[4] = dataStart[2];
                            fakePhyUpdateCompleteEvent[5] = dataStart[3];
                            // write fake event back to original buffer so macOS receives it
                            dataBuffer->writeBytes(0, fakePhyUpdateCompleteEvent, sizeof(fakePhyUpdateCompleteEvent));
                        }
                        skipExtraReadRemoteFeaturesComplete = true;
                    }
                }
            }

            IOFree(buffer, bytesTransferred);
        }
    }

    // Call original completion (if any)
    if (asyncOwner->action)
        asyncOwner->action(asyncOwner->owner, parameter, status, bytesTransferred);
}

/**
 * newAsyncIO
 *
 * Hook IOUSBHostPipe::io or equivalent async IO to swap completion to our wrapper
 * when the pipe instance matches the interrupt endpoint of Intel USB BT device.
 */
IOReturn CIntelBTPatcher::newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t dataBufferLength, IOUSBHostCompletion* completion, uint32_t completionTimeoutMs)
{
    if (gLEFixEnabled && that == _hookPipeInstance && completion) {
        if (!_interruptPipeAsyncOwner)
            _interruptPipeAsyncOwner = new AsyncOwnerData;
        _interruptPipeAsyncOwner->action = completion->action;
        _interruptPipeAsyncOwner->owner = completion->owner;
        _interruptPipeAsyncOwner->dataBuffer = dataBuffer;
        completion->action = asyncIOCompletion;
        completion->owner = _interruptPipeAsyncOwner;
    }
    return FunctionCast(newAsyncIO, callbackIBTPatcher->oldAsyncIO)(that, dataBuffer, dataBufferLength, completion, completionTimeoutMs);
}

/**
 * newInitPipe
 *
 * Detect interrupt endpoint initialisation for Intel USB device and store pipe instance.
 */
int CIntelBTPatcher::newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor, StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor, AppleUSBHostController *controller, IOUSBHostDevice *device, IOUSBHostInterface *interface, unsigned char a7, unsigned short a8)
{
    int ret = FunctionCast(newInitPipe, callbackIBTPatcher->oldInitPipe)(that, descriptor, superDescriptor, controller, device, interface, a7, a8);
    if (!gLEFixEnabled)
        return ret;

    if (device) {
        const StandardUSB::DeviceDescriptor *deviceDescriptor = device->getDeviceDescriptor();
        if (deviceDescriptor && deviceDescriptor->idVendor == VENDOR_USB_INTEL) {
            uint8_t epType = StandardUSB::getEndpointType(descriptor);
            // interrupt endpoint type typically kIOUSBEndpointTypeInterrupt
            if (epType == kIOUSBEndpointTypeInterrupt) {
                CIntelBTPatcher::_hookPipeInstance = that;
                if (!CIntelBTPatcher::_interruptPipeAsyncOwner)
                    CIntelBTPatcher::_interruptPipeAsyncOwner = new AsyncOwnerData;
                CIntelBTPatcher::_randomAddressInit = false;
            }
        }
    }
    return ret;
}

/**
 * newHostDeviceRequest
 *
 * Inspect outgoing HCI commands (host device requests). If we observe LE commands
 * such as LE Set Scan Enable or LE Read Remote Features, we may re-send or log
 * additional HCI commands as per PR#446 strategy. This is a lightweight
 * implementation: we mostly pass-through and log, but includes place-holders
 * where extra HCI commands could be injected if needed.
 */
IOReturn CIntelBTPatcher::newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout) {
    if (!gLEFixEnabled)
        return FunctionCast(newHostDeviceRequest, callbackIBTPatcher->oldHostDeviceRequest)(that, provider, request, data, descriptor, length, completion, timeout);

    // Attempt to parse HCI opcode if descriptor contains command buffer
    if (descriptor && descriptor->getLength()) {
        uint32_t bytes = descriptor->getLength();
        if (bytes >= 3) {
            void *buf = IOMalloc(bytes);
            if (buf) {
                descriptor->readBytes(0, buf, bytes);
                HciCommandHdr *hdr = (HciCommandHdr *)buf;
                if (hdr) {
                    uint16_t opcode = hdr->opcode;
                    if (opcode == HCI_OP_LE_SET_SCAN_ENABLE) {
                        // On first observation, we could send Random Address HCI or LE Set Scan Param here (PR logic)
                        if (!_randomAddressInit) {
                            // placeholder: send random address HCI sequence if necessary
                            IOFree(buf, bytes);
                            // original request remains executed below
                        }
                    } else if (opcode == HCI_OP_LE_READ_REMOTE_FEATURES) {
                        SYSLOG(DRV_NAME, "[PATCH] Observed LE Read Remote Features HCI command");
                    }
                }
                IOFree(buf, bytes);
            }
        }
    }

    return FunctionCast(newHostDeviceRequest, callbackIBTPatcher->oldHostDeviceRequest)(that, provider, request, data, descriptor, length, completion, timeout);
}

/**
 * init
 *
 * Parse boot-arg intelbt_lefix=1 to enable the LE fix at runtime.
 * When enabled, register routes for async IO and initWithDescriptors hooks.
 * Also log to IOConsole for debugging purpose.
 */
bool CIntelBTPatcher::init()
{
    int present = 0;
    if (PE_parse_boot_argn("intelbt_lefix", &present, sizeof(present)) && present == 1) {
        gLEFixEnabled = true;
        IOLog("IntelBTPatcher: LE Fix enabled (via boot-arg)\n");
    } else {
        gLEFixEnabled = false;
    }

    // If original callback structure not present, bail out (preserve original behavior)
    if (!callbackIBTPatcher)
        return false;

    // Register routes only when fix is enabled
    if (gLEFixEnabled) {
        KernelPatcher &patcher = KernelPatcher::getInstance();
        size_t index = patcher.getKextID("IntelBTPatcher");
        if (index != KernelPatcher::KextInfo::Unloaded) {
            KernelPatcher::RouteRequest asyncIORequest {
                "__ZN13IOUSBHostPipe2ioEP18IOMemoryDescriptorjP19IOUSBHostCompletionj",
                newAsyncIO,
                oldAsyncIO
            };
            patcher.routeMultiple(index, &asyncIORequest, 1, nullptr, 0);
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                SYSLOG(DRV_NAME, "routed async IO");
            } else {
                patcher.clearError();
            }

            KernelPatcher::RouteRequest initPipeRequest {
                "__ZN13IOUSBHostPipe28initWithDescriptorsAndOwnersEPKN11StandardUSB18EndpointDescriptorEPKNS0_37SuperSpeedEndpointCompanionDescriptorEP22AppleUSBHostControllerP15IOUSBHostDeviceP18IOUSBHostInterfaceht",
                newInitPipe,
                oldInitPipe
            };
            patcher.routeMultiple(index, &initPipeRequest, 1, nullptr, 0);
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                SYSLOG(DRV_NAME, "routed initWithDescriptors");
            } else {
                patcher.clearError();
            }
        }
    }

    return true;
}
