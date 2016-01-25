//==============================================================================
// Copyright (c) 2014-2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  The DX12Interceptor contains the mechanisms responsible for
///         instrumenting DX12 objects and function calls through hooking.
//==============================================================================

//#define USE_WARP_DEVICE
//#define USE_DEBUG_DEVICE

#include "DX12Interceptor.h"
#include "DX12TraceAnalyzerLayer.h"
#include "Objects/Autogenerated/DX12CoreWrappers.h"
#include "Objects/DX12CreateInfoStructs.h"
#include "Objects/DX12CustomWrappers.h"
#include "DX12LayerManager.h"

#include "../Common/OSWrappers.h"
#include <AMDTOSWrappers/Include/osFilePath.h>
#include <AMDTOSWrappers/Include/osModule.h>
#include <AMDTOSWrappers/Include/osOSDefinitions.h>
#ifndef DLL_REPLACEMENT
    #include "Interceptor.h"
#endif

#include "../Common/SharedGlobal.h"
#include "DX12Defines.h"
//#include "GPUPerfAPIUtil.h"

static const uint64 FIRST_SAMPLE_ID = 0;

//--------------------------------------------------------------------------
/// A map of device handle to DeviceInfo structure, and a lock used when modifying it.
//--------------------------------------------------------------------------
static DeviceToProfilerMap sDeviceToProfilerMap;

//--------------------------------------------------------------------------
/// Get a device's DX12CmdListProfiler instance.
/// \param inWrappedInterface Our wrapped object.
/// \returns The DX12CmdListProfiler instance.
//--------------------------------------------------------------------------
DX12CmdListProfiler* GetProfiler(IUnknown* inWrappedInterface)
{
    GPS_ID3D12GraphicsCommandList* pWrappedGraphicsCommandList = static_cast<GPS_ID3D12GraphicsCommandList*>(inWrappedInterface);

    ID3D12Device* pDevice = nullptr;
    pWrappedGraphicsCommandList->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));
    ID3D12Device* pRealDevice = dynamic_cast<GPS_ID3D12DeviceCustom*>(pDevice)->mRealDevice;

    DX12CmdListProfiler* pProfilerInstance = sDeviceToProfilerMap[pRealDevice];
    PsAssert(pProfilerInstance != NULL);

    return pProfilerInstance;
}

//--------------------------------------------------------------------------
/// Gets a debug interface.
/// \param riid The globally unique identifier (GUID) for the debug interface.
/// \param ppvDebug The debug interface, as a pointer to pointer to void.
/// \returns This method returns one of the Direct3D 12 Return Codes.
//--------------------------------------------------------------------------
HRESULT WINAPI Mine_D3D12GetDebugInterface(_In_ REFIID riid, _COM_Outptr_opt_ void** ppvDebug)
{
    DX12Interceptor* interceptor = GetDX12LayerManager()->GetInterceptor();

    HRESULT getDebugInterfaceResult = interceptor->mHook_GetDebugInterface.mRealHook(riid, ppvDebug);

    return getDebugInterfaceResult;
}

//--------------------------------------------------------------------------
/// The Mine_ version of the hooked D3D12CreateDevice function. This must
/// be hooked in order to create a device wrapper instance.
/// \param pAdapter A pointer to the video adapter to use when creating a device.
/// \param MinimumFeatureLevel The minimum D3D_FEATURE_LEVEL required for successful device creation.
/// \param riid The globally unique identifier (GUID) for the device interface. See remarks.
/// \param ppDevice A pointer to a memory block that receives a pointer to the device.
/// \returns S_OK if device creation succeeded, and an error code if it somehow failed.
//--------------------------------------------------------------------------
HRESULT WINAPI Mine_D3D12CreateDevice(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid, // Expected: ID3D12Device
    void** ppDevice)
{
    DX12Interceptor* interceptor = GetDX12LayerManager()->GetInterceptor();

#ifdef USE_DEBUG_DEVICE
    // Enable the debug layer through the debug interface.
    ID3D12Debug* debugInterface = NULL;

    HRESULT enableDebugResult = Mine_D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&debugInterface);

    if (enableDebugResult == S_OK)
    {
        // Enable the debug layer
        debugInterface->EnableDebugLayer();
    }

#endif

    // Invoke the real runtime CreateDevice function.
    HRESULT deviceCreateResult = interceptor->mHook_CreateDevice.mRealHook(pAdapter, MinimumFeatureLevel, riid, ppDevice);

    // If we're going to collect framerate statistics, don't wrap the device, or anything else that a wrapped device may create.
    // This removes the overhead of wrapped ID3D12 objects, while still wrapping DXGI Swapchains for Present calls.
    if (SG_GET_BOOL(OptionCollectFrameStats) == false)
    {
        if (deviceCreateResult == S_OK && ppDevice != NULL && *ppDevice != NULL)
        {


            GPS_ID3D12DeviceCreateInfo* deviceCreateInfo = new GPS_ID3D12DeviceCreateInfo(pAdapter, MinimumFeatureLevel);
            WrapD3D12Device((ID3D12Device**)ppDevice, deviceCreateInfo);

#if USE_GPA_PROFILING == 0
            ID3D12Device* pAppDevice = static_cast<ID3D12Device*>(*ppDevice);
            ID3D12Device* pRealDevice = dynamic_cast<GPS_ID3D12DeviceCustom*>(pAppDevice)->mRealDevice;

            DX12CmdListProfilerConfig profilerConfig = {};
            profilerConfig.measurementsPerGroup = 256;
            profilerConfig.measurementTypeFlags = PROFILER_MEASUREMENT_TYPE_TIMESTAMPS;

            sDeviceToProfilerMap[pRealDevice] = DX12CmdListProfiler::Create(pRealDevice, &profilerConfig);
#endif
        }
    }

    return deviceCreateResult;
}

//--------------------------------------------------------------------------
/// The Mine_ version of the hooked D3D12SerializeRootSignature function.
/// \param A pointer to a D3D12_ROOT_SIGNATURE structure that describes a root signature.
/// \param A D3D_ROOT_SIGNATURE_VERSION-typed value that specifies the version of root signature.
/// \param A pointer to a memory block that receives a pointer to the ID3DBlob interface that you can use to access the serialized root signature.
/// \param A pointer to a memory block that receives a pointer to the ID3DBlob interface that you can use to access serializer error messages, or NULL if there are no errors.
/// \returns S_OK if successful; otherwise, returns one of the Direct3D 12 Return Codes.
//--------------------------------------------------------------------------
HRESULT WINAPI Mine_D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    DX12Interceptor* interceptor = GetDX12LayerManager()->GetInterceptor();

    HRESULT serializeResult = interceptor->mHook_SerializeRootSignature.mRealHook(pRootSignature, Version, ppBlob, ppErrorBlob);

    return serializeResult;
}

//--------------------------------------------------------------------------
/// The Mine_ version of the hooked D3D12CreateRootSignatureDeserializer function.
/// \param pSrcData
/// \param SrcDataSizeInBytes
/// \param pRootSignatureDeserializerInterface
/// \param ppRootSignatureDeserializer
/// \returns S_OK if successful; otherwise, returns one of the Direct3D 12 Return Codes.
//--------------------------------------------------------------------------
HRESULT WINAPI Mine_D3D12CreateRootSignatureDeserializer(
    LPCVOID pSrcData,
    SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppRootSignatureDeserializer)
{
    DX12Interceptor* interceptor = GetDX12LayerManager()->GetInterceptor();

    HRESULT createResult = interceptor->mHook_CreateRootSignatureDeserializer.mRealHook(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);

    GPS_ID3D12RootSignatureDeserializerCreateInfo* rootSignatureDeserializerCreateInfo = new GPS_ID3D12RootSignatureDeserializerCreateInfo(pSrcData, SrcDataSizeInBytes);
    WrapD3D12RootSignatureDeserializer((ID3D12RootSignatureDeserializer**)ppRootSignatureDeserializer, rootSignatureDeserializerCreateInfo);

    return createResult;
}

//--------------------------------------------------------------------------
/// DX12Interceptor constructor.
//--------------------------------------------------------------------------
DX12Interceptor::DX12Interceptor()
    : mbCollectApiTrace(false)
    , mbProfilerEnabled(false)
    , mSampleIndex(FIRST_SAMPLE_ID)
    , mRealD3D12(NULL)
{
}

//--------------------------------------------------------------------------
/// DX12Interceptor destructor.
//--------------------------------------------------------------------------
DX12Interceptor::~DX12Interceptor()
{
}

//--------------------------------------------------------------------------
/// Retrieve the LayerManager that owns this interceptor.
/// \returns The singleton DX12LayerManager instance.
//--------------------------------------------------------------------------
ModernAPILayerManager* DX12Interceptor::GetParentLayerManager()
{
    return GetDX12LayerManager();
}

//--------------------------------------------------------------------------
/// Method used to initialize the DX12Interceptor and start the hooking process.
/// \returns True if interceptor initialization was successful. False if it somehow failed.
//--------------------------------------------------------------------------
bool DX12Interceptor::Initialize()
{
    bool bInitialized = false;

    // Hook all hooked functions.
    bInitialized = HookInterceptor();

#if USE_GPA_PROFILING
    bInitialized &= InitializeGPA(GPA_API_DIRECTX_12);
#endif

    return bInitialized;
}

//--------------------------------------------------------------------------
/// Initialize everything required for GPA usage.
/// \param inAPI The API that GPA needs to load support for.
/// \returns True if initialized successfully. False if it failed.
//--------------------------------------------------------------------------
bool DX12Interceptor::InitializeGPA(GPA_API_Type inAPI)
{
    const char* errorMessage = NULL;
    bool bLoadSuccessful = mGPALoader.Load(SG_GET_PATH(GPUPerfAPIPath), inAPI, &errorMessage);

    if (!bLoadSuccessful)
    {
        Log(logERROR, "Failed to load GPA. Load error: %s\n", errorMessage);
    }
    else
    {
        if (mGPALoader.GPA_RegisterLoggingCallback(GPA_LOGGING_ERROR_AND_MESSAGE, (GPA_LoggingCallbackPtrType)&DX12Interceptor::GPALoggingCallback) != GPA_STATUS_OK)
        {
            Log(logERROR, "Failed to register profiler logging callback.\n");
        }
    }

    return bLoadSuccessful;
}

//--------------------------------------------------------------------------
/// A logging callback for use with GPA. When GPA logs a message, it is sent here.
/// \param messageType The type of message being logged.
/// \param message The message string to be logged.
//--------------------------------------------------------------------------
void DX12Interceptor::GPALoggingCallback(GPA_Logging_Type messageType, const char* message)
{
    // Dump a message with the correct GPS log type to match GPA. Fall back to logRAW if necessary.
    LogType logType = (messageType == GPA_LOGGING_ERROR) ? logERROR : ((messageType == GPA_LOGGING_MESSAGE) ? logMESSAGE : ((messageType == GPA_LOGGING_TRACE) ? logTRACE : logRAW));
    Log(logType, "GPA: %s\n", message);
}

//--------------------------------------------------------------------------
/// Method used to shutdown the DX12Interceptor. Detaching hooked functions happens here.
/// \returns True if detaching/shutdown was successful. False if it failed.
//--------------------------------------------------------------------------
bool DX12Interceptor::Shutdown()
{
    UnhookInterceptor();

    return true;
}

//--------------------------------------------------------------------------
/// Given a ThreadID, reserve the next available SampleId for the call that's about to happen in the thread.
/// \param inThreadId The Id of the thread where the profiled function is about to be invoked.
/// \returns A new unique Sample Id to be used in profiling with GPA.
//--------------------------------------------------------------------------
gpa_uint32 DX12Interceptor::SetNextSampleId(DWORD inThreadId)
{
    gpa_uint32 nextId = GetNextSampleId();

    SampleInfo& sampleInfo = mSampleIdMap[inThreadId];
    sampleInfo.mSampleId = nextId;
    sampleInfo.mbBeginSampleSuccessful = false;

    return nextId;
}

//--------------------------------------------------------------------------
/// Retrieve the next available unique Sample Id.
/// \returns A unique Sample Id to be used with GPA.
//--------------------------------------------------------------------------
gpa_uint32 DX12Interceptor::GetNextSampleId()
{
    ScopeLock sampleLock(&mUniqueSampleIdMutex);
    mSampleIndex++;
    return mSampleIndex;
}

//--------------------------------------------------------------------------
/// Reset the Sample Id for the next frame.
//--------------------------------------------------------------------------
void DX12Interceptor::ResetSampleIdCounter()
{
    // Probably don't need to lock, but doesn't hurt.
    ScopeLock sampleLock(&mUniqueSampleIdMutex);
    mSampleIndex = FIRST_SAMPLE_ID;
    mSampleIdMap.clear();

    // Clear the count of samples per command list when starting a new frame.
    mSamplesPerCommandList.clear();
}

//--------------------------------------------------------------------------
/// Responsible for the pre-call instrumentation of every DX12 API call.
/// \param inFuncId The FuncId for the function that's about to be invoked.
//--------------------------------------------------------------------------
void DX12Interceptor::PreCall(IUnknown* inWrappedInterface, FuncId inFunctionId)
{
    osThreadId threadId = osGetCurrentThreadId();

    DX12TraceAnalyzerLayer* traceAnalyzerLayer = DX12TraceAnalyzerLayer::Instance();
    const char* funcName = traceAnalyzerLayer->GetFunctionNameFromId(inFunctionId);
    Log(logTRACE, "Precall in '%s'.\n", funcName);

    // Check if we intend to collect GPU time, and set up GPA to do all the work.
    if (ShouldCollectGPUTime() && traceAnalyzerLayer->ShouldProfileFunction(inFunctionId))
    {
        // Don't let new sampling begin until this sample is finished with a call to EndSample.
        LockGPA();

        // The only inWrappedInterfaces that get to this point are wrapped ID3D12GraphicsCommandList interfaces.
        // Cast to the wrapper type, and then pass in the real runtime interface.
        GPS_ID3D12GraphicsCommandList* pWrappedGraphicsCommandList = static_cast<GPS_ID3D12GraphicsCommandList*>(inWrappedInterface);
#if USE_GPA_PROFILING
        if (pWrappedGraphicsCommandList != NULL)
        {
            // Count the number of samples that have been collected using this commandlist.
            CommandListToSampleCountMap::iterator sampleCountIter = mSamplesPerCommandList.find(pWrappedGraphicsCommandList);
            if (sampleCountIter == mSamplesPerCommandList.end())
            {
                mSamplesPerCommandList[pWrappedGraphicsCommandList] = 1;
            }
            else
            {
                // Increment the per-commandlist count and compare to the max.
                gpa_uint32& count = sampleCountIter->second;
                count++;
            }

            SampleInfo& sampleInfo = mSampleIdMap[threadId];

            // Get the next sample ID to use for this call. Associate this thread with the sample ID.
            gpa_uint32 sampleId = SetNextSampleId(threadId);
            SwitchGPAContext(pWrappedGraphicsCommandList->mRealGraphicsCommandList);

            sampleInfo.mSampleId = sampleId;

            GPA_Status beginStatus = mGPALoader.GPA_BeginSample(sampleId);

            sampleInfo.mbBeginSampleSuccessful = (beginStatus == GPA_STATUS_OK);

            if (beginStatus == GPA_STATUS_OK)
            {
                traceAnalyzerLayer->AddSampleToCommandList(pWrappedGraphicsCommandList->mRealGraphicsCommandList, sampleId);
            }
            else
            {
                Log(logERROR, "Failed to begin sampling for current entry.\n");
            }
        }
        else
        {
            Log(logERROR, "Failed to unwrap the incoming wrapper interface '0x%p'.\n", inWrappedInterface);
        }

#else
        ID3D12Device* pDevice = nullptr;
        pWrappedGraphicsCommandList->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));

        ID3D12Device* pRealDevice = dynamic_cast<GPS_ID3D12DeviceCustom*>(pDevice)->mRealDevice;

        DX12CmdListProfiler* pProfilerInstance = sDeviceToProfilerMap[pRealDevice];
        PsAssert(pProfilerInstance != NULL);

        ProfilerMeasurementId measurementId = ConstructMeasurementInfo(inFunctionId, SetNextSampleId(threadId), pWrappedGraphicsCommandList->mRealGraphicsCommandList);

        pProfilerInstance->BeginCmdMeasurement(pWrappedGraphicsCommandList->mRealGraphicsCommandList, &measurementId);
#endif

    }
    else
    {
        Log(logTRACE, "Did not profile '%s'.\n", funcName);
    }

    Log(logTRACE, "Thread %d\t Precall\n", threadId);
    traceAnalyzerLayer->BeforeAPICall();
}

//--------------------------------------------------------------------------
/// Responsible for the post-call instrumentation of every DX12 API call.
/// \param inWrapperInstance An instance of a wrapped DX12 interface.
/// \param inFunctionId The function ID for the call being traced.
/// \param inArgumentString A string containing the stringified comma-separated arguments for the API call.
/// \param inReturnValue The return value for the function. If void, use FUNCTION_RETURNS_VOID.
//--------------------------------------------------------------------------
void DX12Interceptor::PostCall(IUnknown* inWrappedInterface, FuncId inFunctionId, const char* inArgumentString, INT64 inReturnValue)
{
    osThreadId threadId = osGetCurrentThreadId();

    DX12TraceAnalyzerLayer* pTraceAnalyzerLayer = DX12TraceAnalyzerLayer::Instance();
    const char* funcName = pTraceAnalyzerLayer->GetFunctionNameFromId(inFunctionId);
    Log(logTRACE, "Thread %d\t Postcall: %s\n", threadId, funcName);

    DX12APIEntry* pNewEntry = pTraceAnalyzerLayer->LogAPICall(inWrappedInterface, inFunctionId, inArgumentString, inReturnValue);

    // Wait and gather results
    if (ShouldCollectGPUTime() && pTraceAnalyzerLayer->ShouldProfileFunction(inFunctionId))
    {

#if USE_GPA_PROFILING
        // Determine which SampleID is associated with this call and insert it into the DX12APIEntry.
        ThreadIdToSampleIdMap::iterator threadSampleIter = mSampleIdMap.find(threadId);

        if (threadSampleIter != mSampleIdMap.end())
        {
            SampleInfo& sampleInfo = threadSampleIter->second;

            // We can call EndSample, begin BeginSample was called successfully.
            if (sampleInfo.mbBeginSampleSuccessful)
            {
                GPA_Status endSampleStatus = mGPALoader.GPA_EndSample();

                if (endSampleStatus == GPA_STATUS_OK)
                {
                    pNewEntry->mSampleId = sampleInfo.mSampleId;

                    pTraceAnalyzerLayer->StoreProfilerResult(pNewEntry);
                    Log(logTRACE, "BeginSample with Id '%u'.\n", pNewEntry->mSampleId);
                }
                else
                {
                    Log(logERROR, "Failed to end sampling for new entry.\n");
                }
            }
            else
            {
                Log(logTRACE, "BeginSample failed. Not invoking EndSample.\n");
            }
        }
        else
        {
            Log(logERROR, "Didn't call EndSample because BeginSample wasn't successful.\n");
        }

#else
        GPS_ID3D12GraphicsCommandList* pWrappedGraphicsCommandList = static_cast<GPS_ID3D12GraphicsCommandList*>(inWrappedInterface);

        // Determine which SampleID is associated with this call and insert it into the DX12APIEntry.
        ThreadIdToSampleIdMap::iterator threadSampleIter = mSampleIdMap.find(threadId);

        if (threadSampleIter != mSampleIdMap.end())
        {
            SampleInfo& sampleInfo = threadSampleIter->second;

            DX12CmdListProfiler* pProfilerInstance = GetProfiler(inWrappedInterface);
            pProfilerInstance->EndCmdMeasurement(pWrappedGraphicsCommandList->mRealGraphicsCommandList);

            pNewEntry->mSampleId = sampleInfo.mSampleId;
            pTraceAnalyzerLayer->StoreProfilerResult(pNewEntry);
        }
#endif

        // The pass's sample has just ended. Other threads are free to acquire the lock in PreCall above.
        UnlockGPA();
    }
    else
    {
        Log(logTRACE, "Did not profile '%s'.\n", funcName);
    }
}

//--------------------------------------------------------------------------
/// Gather profiler results.
/// \param inWrappedInterface The interface responsible for the profiled call.
/// \paramNumCommandLists The number of CommandLists in the array being executed.
/// \param ppCommandLists The array of CommandLists to be executed.
//--------------------------------------------------------------------------
void DX12Interceptor::GatherProfilerResults(IUnknown* inWrappedInterface, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
{
    DX12CmdListProfiler* pProfilerInstance = GetProfiler(inWrappedInterface);

    DX12TraceAnalyzerLayer* pTraceAnalyzerLayer = DX12TraceAnalyzerLayer::Instance();

    for (UINT i = 0; i < NumCommandLists; i++)
    {
        std::vector<ProfilerResult> results;

        GPS_ID3D12GraphicsCommandList* pWrappedGraphicsCommandList = static_cast<GPS_ID3D12GraphicsCommandList*>(ppCommandLists[i]);

        pProfilerInstance->GetCmdListResults(pWrappedGraphicsCommandList->mRealGraphicsCommandList, static_cast<ID3D12CommandQueue*>(inWrappedInterface), results);

        pTraceAnalyzerLayer->StoreProfilerResults(static_cast<GPS_ID3D12CommandQueue*>(inWrappedInterface), results);
    }
}

//--------------------------------------------------------------------------
/// Switch the GPA context to a ID3D12GraphicsCommandList instance.
/// \param inCurrentContext The context to internally switch to within GPA.
//--------------------------------------------------------------------------
void DX12Interceptor::SwitchGPAContext(ID3D12GraphicsCommandList* inCurrentContext)
{
    // Switch the GPA context only when the GPU trace collection is actually taking place. Otherwise, ignore the call.
    if (ShouldCollectGPUTime())
    {
        GPA_Status selectStatus = mGPALoader.GPA_SelectContext(inCurrentContext);

        if (selectStatus != GPA_STATUS_OK)
        {
            Log(logERROR, "Selecting context '0x%p' in GPA failed with error code '%d'.\n", inCurrentContext, selectStatus);
        }
    }
}

//--------------------------------------------------------------------------
/// Lock GPA access mutex
//--------------------------------------------------------------------------
void DX12Interceptor::LockGPA()
{
    mGPAPrePostMatcherMutex.Lock();
}

//--------------------------------------------------------------------------
/// Unlock GPA access mutex
//--------------------------------------------------------------------------
void DX12Interceptor::UnlockGPA()
{
    mGPAPrePostMatcherMutex.Unlock();
}

//--------------------------------------------------------------------------
/// Hook DX12 entrypoints. Report any errors encountered.
/// \returns Returns true if hooking was successful, and false if it failed.
//--------------------------------------------------------------------------
bool DX12Interceptor::HookInterceptor()
{
    bool bThisInitialized = false;
    char* errorString = NULL;

    gtString moduleFilename;
    moduleFilename.fromASCIIString(DX12_DLL);
    osFilePath modulePath(moduleFilename);
    osModuleHandle d3dModuleHandle;

#ifdef DLL_REPLACEMENT
    // check to see if DX12 is loaded already. If not, go get it
    bool success = true;

    if (!osGetLoadedModuleHandle(modulePath, d3dModuleHandle))
    {
        if (!osLoadModule(modulePath, d3dModuleHandle))
        {
            success = false;
        }
    }

    if (success)
    {
        PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterface_funcPtr;
        PFN_D3D12_CREATE_DEVICE D3D12CreateDevice_funcPtr;
        PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignature_funcPtr;
        PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER D3D12CreateRootSignatureDeserializer_funcPtr;

        D3D12CreateDevice_funcPtr = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(mRealD3D12, "D3D12CreateDevice");
        D3D12SerializeRootSignature_funcPtr = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(mRealD3D12, "D3D12SerializeRootSignature");
        D3D12CreateRootSignatureDeserializer_funcPtr = (PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER)GetProcAddress(mRealD3D12, "D3D12CreateRootSignatureDeserializer");
        D3D12GetDebugInterface_funcPtr = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(mRealD3D12, "D3D12GetDebugInterface");

        mHook_GetDebugInterface.SetHooks(D3D12GetDebugInterface_funcPtr, Mine_D3D12GetDebugInterface);
        mHook_CreateDevice.SetHooks(D3D12CreateDevice_funcPtr, Mine_D3D12CreateDevice);
        mHook_SerializeRootSignature.SetHooks(D3D12SerializeRootSignature_funcPtr, Mine_D3D12SerializeRootSignature);
        mHook_CreateRootSignatureDeserializer.SetHooks(D3D12CreateRootSignatureDeserializer_funcPtr, Mine_D3D12CreateRootSignatureDeserializer);
        bThisInitialized = true;
    }

#else

    // Attempt to load the where the hooked functions reside.
    if (osLoadModule(modulePath, d3dModuleHandle))
    {
        // Initialize hooking, and then hook the list of entrypoints below.
        LONG hookError = AMDT::BeginHook();

        if (hookError == NO_ERROR)
        {
            // D3D12GetDebugInterface
            osProcedureAddress D3D12GetDebugInterface_funcPtr;

            if (osGetProcedureAddress(d3dModuleHandle, "D3D12GetDebugInterface", D3D12GetDebugInterface_funcPtr, true) == true)
            {
                mHook_GetDebugInterface.SetHooks((PFN_D3D12_GET_DEBUG_INTERFACE)D3D12GetDebugInterface_funcPtr, Mine_D3D12GetDebugInterface);
                bThisInitialized = mHook_GetDebugInterface.Attach();
            }
            else
            {
                Log(logERROR, "Failed to initialize hook for export with name '%s'.", "D3D12GetDebugInterface");
            }

            // D3D12CreateDevice
            osProcedureAddress D3D12CreateDevice_funcPtr;

            if (osGetProcedureAddress(d3dModuleHandle, "D3D12CreateDevice", D3D12CreateDevice_funcPtr, true) == true)
            {
                mHook_CreateDevice.SetHooks((PFN_D3D12_CREATE_DEVICE)D3D12CreateDevice_funcPtr, Mine_D3D12CreateDevice);
                bThisInitialized = mHook_CreateDevice.Attach();
            }
            else
            {
                Log(logERROR, "Failed to initialize hook for export with name '%s'.", "D3D12CreateDevice");
            }

            // D3D12SerializeRootSignature
            osProcedureAddress D3D12SerializeRootSignature_funcPtr;

            if (osGetProcedureAddress(d3dModuleHandle, "D3D12SerializeRootSignature", D3D12SerializeRootSignature_funcPtr, true) == true)
            {
                mHook_SerializeRootSignature.SetHooks((PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)D3D12SerializeRootSignature_funcPtr, Mine_D3D12SerializeRootSignature);
                bThisInitialized = mHook_SerializeRootSignature.Attach();
            }
            else
            {
                Log(logERROR, "Failed to initialize hook for export with name '%s'.", "D3D12SerializeRootSignature");
            }

            // D3D12CreateRootSignatureDeserializer
            osProcedureAddress D3D12CreateRootSignatureDeserializer_funcPtr;

            if (osGetProcedureAddress(d3dModuleHandle, "D3D12CreateRootSignatureDeserializer", D3D12CreateRootSignatureDeserializer_funcPtr, true) == true)
            {
                mHook_CreateRootSignatureDeserializer.SetHooks((PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER)D3D12CreateRootSignatureDeserializer_funcPtr, Mine_D3D12CreateRootSignatureDeserializer);
                bThisInitialized = mHook_CreateRootSignatureDeserializer.Attach();
            }
            else
            {
                Log(logERROR, "Failed to initialize hook for export with name '%s'.", "D3D12SerializeRootSignature");
            }

            // Commit the hooked calls.
            hookError = AMDT::EndHook();

            if (hookError != NO_ERROR)
            {
                errorString = "EndHook Failed.";
            }
        }
        else
        {
            errorString = "BeginHook Failed.";
        }
    }
    else
    {
        errorString = "Failed to find API module to hook.";
    }

#endif // DLL_REPLACEMENT

    if (!bThisInitialized)
    {
        Log(logERROR, "%s failed to hook: %s\n", __FUNCTION__, errorString);
    }

    return bThisInitialized;
}

//--------------------------------------------------------------------------
/// Detach all hooked entrypoints prior to shutting the interceptor down.
/// \returns True if detaching was successful. False if it failed.
//--------------------------------------------------------------------------
bool DX12Interceptor::UnhookInterceptor()
{
    bool bDetachSuccessful = true;

#ifndef DLL_REPLACEMENT
    char* errorMessage = NULL;

    LONG hookError = NO_ERROR;

    hookError = AMDT::BeginHook();

    if (hookError == NO_ERROR)
    {
        bDetachSuccessful &= mHook_CreateDevice.Detach();
        bDetachSuccessful &= mHook_SerializeRootSignature.Detach();
        bDetachSuccessful &= mHook_SerializeRootSignature.Detach();

        if (bDetachSuccessful)
        {
            hookError = AMDT::EndHook();

            if (hookError == NO_ERROR)
            {
                bDetachSuccessful = true;
            }
            else
            {
                errorMessage = "EndHook failed.";
            }
        }
        else
        {
            errorMessage = "Detaching hooks failed.";
        }
    }
    else
    {
        errorMessage = "BeginHook failed.";
    }

    if (!bDetachSuccessful)
    {
        Log(logERROR, "DX12Interceptor failed to detach successfully: %s\n", errorMessage);
    }

#endif // DLL_REPLACEMENT

    return bDetachSuccessful;
}

//--------------------------------------------------------------------------
/// Construct a measurement info structure for each call that will be profiled.
/// \param inFuncId The function ID for the function being profiled.
/// \param inSampleId The SampleID associated with the profiled command.
/// \param pCmdList The command list that executed the profiled command.
//--------------------------------------------------------------------------
ProfilerMeasurementId DX12Interceptor::ConstructMeasurementInfo(FuncId inFuncId, UINT64 sampleId, ID3D12GraphicsCommandList* pCmdList)
{
    DX12LayerManager* pLayerManager = GetDX12LayerManager();
    ProfilerMeasurementId idInfo = {};

    idInfo.pCmdList = pCmdList;

    idInfo.sampleId = sampleId;

    idInfo.frame = pLayerManager->GetFrameCount();

    idInfo.funcId = inFuncId;

    return idInfo;
}

#ifdef DLL_REPLACEMENT

// The replaced functions.
HRESULT WINAPI D3D12GetDebugInterface(_In_ REFIID riid, _COM_Outptr_opt_ void** ppvDebug)
{
    CheckUpdateHooks();
    return Mine_D3D12GetDebugInterface(riid, ppvDebug);
}

HRESULT WINAPI D3D12CreateDevice(
    _In_opt_ IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ REFIID riid, // Expected: ID3D12Device
    _COM_Outptr_opt_ void** ppDevice)
{
    CheckUpdateHooks();
    return Mine_D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
}

HRESULT WINAPI D3D12SerializeRootSignature(
    _In_ const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    CheckUpdateHooks();
    return Mine_D3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer)
{
    CheckUpdateHooks();
    return Mine_D3D12CreateRootSignatureDeserializer(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);
}
#endif // DLL_REPLACEMENT