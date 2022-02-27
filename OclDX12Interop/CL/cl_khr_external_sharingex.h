#ifndef __OPENCL_CL_EXTERNAL_H
#define __OPENCL_CL_EXTERNAL_H

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif	

#ifdef __cplusplus
extern "C" {
#endif

#define CL_INVALID_EXTERNAL_DEVICEGROUP_REFERENCE_KHR   -1102
#define CL_INVALID_EXT_MEM_DESC_KHR                     -1103

#define CL_CURRENT_DEVICE_FOR_EXTERNAL_CONTEXT_KHR      0x2036
#define CL_DEVICES_FOR_EXTERNAL_CONTEXT_KHR             0x2037
#define CL_EXTERNAL_DEVICE_KHR                          0x2038
#define CL_EXTERNAL_DEVICEGROUP_KHR                     0x2039

#define CL_COMMAND_SEMAPHORE_WAIT_KHR                   0x2040
#define CL_COMMAND_SEMAPHORE_SIGNAL_KHR                 0x2041
#define CL_COMMAND_ACQUIRE_EXTERNAL_MEM_OBJECTS_KHR     0x2042
#define CL_COMMAND_RELEASE_EXTERNAL_MEM_OBJECTS_KHR     0x2043

#define CL_EXTERNAL_DEVICE_UUID_KHR                     0x2044

#define CL_EXTERNAL_IMAGE_INFO                          0x2045

typedef cl_uint cl_external_context_info;

typedef void* cl_external_device;

typedef enum _cl_external_context_type_enum {
    CL_EXTERNAL_CONTEXT_TYPE_VULKAN = 1,
    CL_EXTERNAL_CONTEXT_TYPE_DX12 = 2,
}cl_external_context_type;

typedef enum _cl_external_mem_handle_type_enum {
    CL_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD= 1,
    CL_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CL_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    CL_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP   = 4,
    CL_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE   = 5,
} cl_external_mem_handle_type;

typedef enum _cl_semaphore_handle_type__enum {
    CL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD= 1,
    CL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    CL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    CL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE  = 4,
} cl_semaphore_handle_type;

typedef enum _cl_semaphore_type__enum {
    CL_SEMAPHORE_TYPE_BINARY= 1,
    CL_SEMAPHORE_TYPE_TIMELINE  = 2,
} cl_semaphore_type;

typedef enum _cl_external_mem_properties {
    CL_EXTERNAL_MEMORY_HANDLE_TYPE = 1, // Value should be of type cl_external_mem_handle_type
    CL_EXTERNAL_MEMORY_HANDLE_SIZE = 2, // Value should be of type cl_uint
} cl_external_mem_properties;


typedef struct _cl_external_mem_desc_st {
    cl_external_mem_handle_type type;
    void *handle;
    size_t offset;
    cl_external_mem_properties *props;
    unsigned long long size;
} cl_external_mem_desc;


typedef enum _cl_semaphore_properties_khr {
    CL_SEMAPHORE_HANDLE_TYPE = 1, // Value should be of type cl_semaphore_handle_type
    CL_SEMAPHORE_HANDLE_SIZE = 2, // Value should be of type cl_uint
    CL_SEMAPHORE_TYPE= 3, // Value should be of type cl_semaphore_type
} cl_semaphore_properties_khr;

typedef struct _cl_semaphore_desc_st {
    cl_semaphore_handle_type type;
    void *handle;
} cl_semaphore_desc;


typedef cl_long cl_mem_properties_khr;
typedef struct _cl_semaphore* cl_semaphore;
typedef cl_ulong cl_semaphore_payload;

extern CL_API_ENTRY cl_int CL_API_CALL
clGetExternalContextInfoKHR (const cl_context_properties  *properties,
                             cl_external_context_info  param_name,
                             size_t  param_value_size,
                             void  *param_value,
                             size_t  *param_value_size_ret)  CL_API_SUFFIX__VERSION_1_2;


extern CL_API_ENTRY cl_mem CL_API_CALL
clCreateBufferFromExternalMemoryKHR(cl_context context,
                                    const cl_mem_properties_khr* properties,
                                    cl_mem_flags flags,
                                    cl_external_mem_desc extMem,
                                    cl_int *errcode_ret)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_mem CL_API_CALL
clCreateImageFromExternalMemoryKHR(cl_context context,
                                   cl_mem_flags flags,
                                   cl_external_mem_desc extMem,
                                   const cl_image_format *image_format,
                                   const cl_image_desc *image_desc,
                                   cl_int *errcode_ret)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_semaphore CL_API_CALL
clCreateFromExternalSemaphoreKHR (cl_context context,
                                  cl_semaphore_properties_khr sema_props,
                                  cl_semaphore_desc sema_desc,
                                  cl_int *errcode_ret)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWaitForSemaphoresKHR (cl_command_queue command_queue,
                               cl_uint num_sema_descs,
                               const cl_semaphore *sema_list,
                               const cl_semaphore_payload *sema_payload_list,
                               cl_uint num_events_in_wait_list,
                               const cl_event *event_wait_list,
                               cl_event *event)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_int CL_API_CALL
clEnqueueSignalSemaphoresKHR (cl_command_queue command_queue,
                              cl_uint num_sema_descs,
                              const cl_semaphore *sema_list,
                              const cl_semaphore_payload *sema_payload_list,
                              cl_uint num_events_in_wait_list,
                              const cl_event *event_wait_list,
                              cl_event *event)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_int CL_API_CALL
clEnqueueAcquireExternalMemObjectsKHR (cl_command_queue command_queue,
                                       cl_uint num_mem_objects,
                                       const cl_mem *mem_objects,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReleaseExternalMemObjectsKHR (cl_command_queue command_queue,
                                       cl_uint num_mem_objects,
                                       const cl_mem *mem_objects,
                                       cl_uint num_events_in_wait_list,
                                       const cl_event *event_wait_list,
                                       cl_event *event)  CL_API_SUFFIX__VERSION_1_2;

extern CL_API_ENTRY cl_int CL_API_CALL
clReleaseSemaphoreObjectKHR (cl_semaphore sema_object) CL_API_SUFFIX__VERSION_1_2;
#ifdef __cplusplus
}
#endif
#endif /*__OPENCL_CL_EXTERNAL_H*/

