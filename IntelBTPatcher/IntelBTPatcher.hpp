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
    void *dataBuffer;
    void *action;
} AsyncOwnerData;

class CIntelBTPatcher {
public:
    bool init();
    void free();
    
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    
    // Các hàm hook
    static IOReturn newFindQueueRequest(void *that, unsigned short arg1, void *addr, unsigned short arg2, bool arg3, void **hciRequestPtr);
    static IOReturn newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length,IOUSBHostCompletion *completion, unsigned int timeout);
    
    // Các hàm hook mới từ PR #446
    static IOReturn newAsyncIO(void *that, IOMemoryDescriptor* dataBuffer, uint32_t bytesTransferred, IOUSBHostCompletion* completion, unsigned int completionTimeoutMs);
    static int newInitPipe(void *that, StandardUSB::EndpointDescriptor const *descriptor, StandardUSB::SuperSpeedEndpointCompanionDescriptor const *superDescriptor, AppleUSBHostController *controller, IOUSBHostDevice *device, IOUSBHostInterface *interface, unsigned char a7, unsigned short a8);
    
    // Phương thức mới để kiểm tra trạng thái PR #446
    bool isPR446Enabled();
    
    // Các con trỏ hàm gốc
    mach_vm_address_t oldFindQueueRequest {};
    mach_vm_address_t oldHostDeviceRequest {};
    mach_vm_address_t oldAsyncIO {};
    mach_vm_address_t oldInitPipe {};
    
private:
    // Biến thành viên static
    static void *_hookPipeInstance;
    static AsyncOwnerData *_interruptPipeAsyncOwner;
    static bool _randomAddressInit;
    static bool _enablePR446;
};

#endif /* IntelBTPatcher_h */
