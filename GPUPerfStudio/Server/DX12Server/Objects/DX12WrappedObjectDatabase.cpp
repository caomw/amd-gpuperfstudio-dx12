//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  The DX12WrappedObjectDatabase is responsible for tracking
///         wrapped and destroyed objects.
//==============================================================================

#include "DX12WrappedObjectDatabase.h"
#include "../../Common/IInstanceBase.h"
#include "IDX12InstanceBase.h"
#include "DX12ObjectDatabaseProcessor.h"
#include "Autogenerated/DX12CoreWrappers.h"

//--------------------------------------------------------------------------
/// Default constructor initializes a new instance of the DX12WrappedObjectDatabase class.
//--------------------------------------------------------------------------
DX12WrappedObjectDatabase::DX12WrappedObjectDatabase()
{
}

//--------------------------------------------------------------------------
/// Destructor destroys the DX12WrappedObjectDatabase instance.
//--------------------------------------------------------------------------
DX12WrappedObjectDatabase::~DX12WrappedObjectDatabase()
{
}

//--------------------------------------------------------------------------
/// Retrieve a list of stored database objects with the same given type.
/// \param inObjectType The type of object to search the database for.
/// \param outObjectInstancesOfGivenType A list containing same-typed object instances.
//--------------------------------------------------------------------------
void DX12WrappedObjectDatabase::GetObjectsByType(eObjectType inObjectType, WrappedInstanceVector& outObjectInstancesOfGivenType, bool inbOnlyCurrentObjects) const
{
    // Loop through everything in the object database and look for specific types of objects.
    DXInterfaceToWrapperMetadata::const_iterator wrapperIter;
    DXInterfaceToWrapperMetadata::const_iterator endIter = mWrapperInstanceToWrapperMetadata.end();

    for (wrapperIter = mWrapperInstanceToWrapperMetadata.begin(); wrapperIter != endIter; ++wrapperIter)
    {
        IDX12InstanceBase* wrapperMetadata = wrapperIter->second;

        if (wrapperMetadata->GetObjectType() == inObjectType)
        {
            // If we only care about currently-active objects, don't include it if it has already been destroyed.
            if (inbOnlyCurrentObjects && wrapperMetadata->IsDestroyed())
            {
                continue;
            }

            outObjectInstancesOfGivenType.push_back(wrapperMetadata);
        }
    }
}

//--------------------------------------------------------------------------
/// Retrieve a pointer to the wrapper instance of the given ID3D12 object.
/// \param inInstanceHandle A handle to the ID3D12 interface instance.
/// \returns A IInstanceBase pointer, which is the wrapper for the given input instance.
//--------------------------------------------------------------------------
IInstanceBase* DX12WrappedObjectDatabase::GetWrappedInstance(void* inInstanceHandle) const
{
    // Look in the wrapped object database for the incoming pointer.
    IUnknown* wrappedInstance = (IUnknown*)inInstanceHandle;

    DXInterfaceToWrapperMetadata::const_iterator interfaceIter = mRealInstanceToWrapperMetadata.find(wrappedInstance);
    DXInterfaceToWrapperMetadata::const_iterator endIter = mRealInstanceToWrapperMetadata.end();

    if (interfaceIter != endIter)
    {
        return interfaceIter->second;
    }
    else
    {
        // Is the incoming pointer already a wrapped pointer? If so, just return the metadata object.
        DXInterfaceToWrapperMetadata::const_iterator wrapperInterfaceIter = mWrapperInstanceToWrapperMetadata.find(wrappedInstance);
        DXInterfaceToWrapperMetadata::const_iterator wrappersEnditer = mWrapperInstanceToWrapperMetadata.end();

        if (wrapperInterfaceIter != wrappersEnditer)
        {
            return wrapperInterfaceIter->second;
        }
    }

    return NULL;
}

//--------------------------------------------------------------------------
/// A handler that's invoked when a device is destroyed. Responsible for cleaning up wrappers.
/// \param inDeviceInstance A wrapper instance for the device being destroyed.
//--------------------------------------------------------------------------
void DX12WrappedObjectDatabase::OnDeviceDestroyed(IInstanceBase* inDeviceInstance)
{
    // @DX12TODO: Clean up wrappers related to the destroyed object.
    (void)inDeviceInstance;
}

//--------------------------------------------------------------------------
/// Attempt to unwrap the given pointer so that it points to the real ID3D12 runtime interface instance.
/// \param inPossiblyWrapperInstance A pointer to either a D3D12 interface instance, or a IDX12InstanceBase wrapper.
/// \returns True if the pointer was already unwrapped, or has been successfully unwrapped. False if the pointer is not in the database.
//--------------------------------------------------------------------------
bool DX12WrappedObjectDatabase::AttemptUnwrap(IUnknown** ioPossiblyWrapperInstance)
{
    bool bUnwrappedInstance = false;

    IUnknown* possibleReal = *ioPossiblyWrapperInstance;

    // Is the incoming pointer a wrapped DX12 interface? Check the wrapper map to find out.
    DXInterfaceToWrapperMetadata::const_iterator wrapperIter = mWrapperInstanceToWrapperMetadata.find(possibleReal);

    if (wrapperIter != mWrapperInstanceToWrapperMetadata.end())
    {
        // The incoming pointer was a GPS_ID3D12 wrapped interface. Unwrap it here and send it back out.
        IDX12InstanceBase* wrapperMetadata = wrapperIter->second;
        *ioPossiblyWrapperInstance = static_cast<IUnknown*>(wrapperMetadata->GetRuntimeInstance());
        bUnwrappedInstance = true;
    }
    else
    {
        Log(logWARNING, "Failed to unwrap instance '0x%p'. Likely wasn't a wrapped interface.\n", possibleReal);
    }

    return bUnwrappedInstance;
}

//--------------------------------------------------------------------------
/// Add a wrapper metadata object to the object database.
/// \param inWrapperMetadata A pointer to the IDX12InstanceBase wrapper metadata instance.
//--------------------------------------------------------------------------
void DX12WrappedObjectDatabase::Add(IDX12InstanceBase* inWrapperMetadata)
{
    ScopeLock mutex(&mCurrentObjectsMapLock);
    IUnknown* wrapperHandle = static_cast<IUnknown*>(inWrapperMetadata->GetApplicationHandle());
    IUnknown* runtimeInstance = inWrapperMetadata->GetRuntimeInstance();

    mWrapperInstanceToWrapperMetadata[wrapperHandle] = inWrapperMetadata;
    mRealInstanceToWrapperMetadata[runtimeInstance] = inWrapperMetadata;
}

//--------------------------------------------------------------------------
/// Retrieve the IDX12InstanceBase metadata object corresponding to the given input wrapper.
/// \param An GPS_ID3D12[Something] interface wrapper.
/// \returns The IDX12InstanceBase metadata object associated with the given wrapper, or NULL if it doesn't exist.
//--------------------------------------------------------------------------
IDX12InstanceBase* DX12WrappedObjectDatabase::GetMetadataObject(IUnknown* inWrapperInstance)
{
    IDX12InstanceBase* resultInstance = NULL;

    DXInterfaceToWrapperMetadata::const_iterator objectIter = mWrapperInstanceToWrapperMetadata.find(inWrapperInstance);

    if (objectIter != mWrapperInstanceToWrapperMetadata.end())
    {
        resultInstance = objectIter->second;
    }

    return resultInstance;
}

//--------------------------------------------------------------------------
/// Retrieve a pointer to the wrapper instance when given a pointer to the real instance.
/// \param ioInstance A pointer to the real ID3D12 interface instance.
/// \returns True if the object is already in the DB, false if it's not.
/// Note: When true, ioInstance is reassigned to the wrapper instance.
//--------------------------------------------------------------------------
bool DX12WrappedObjectDatabase::WrappedObject(IUnknown** ioInstance) const
{
    ScopeLock dbLock(&mCurrentObjectsMapLock);

    IUnknown* pReal = *ioInstance;

    // Check if object has been wrapped
    DXInterfaceToWrapperMetadata::const_iterator objectIter = mRealInstanceToWrapperMetadata.find(pReal);

    if (objectIter == mRealInstanceToWrapperMetadata.end())
    {
        return false;
    }
    else
    {
        // The object already has a wrapper instance in the database. Return the GPS_ID3D12[Something] wrapper instance.
        IDX12InstanceBase* wrapperMetaData = static_cast<IDX12InstanceBase*>(objectIter->second);
        *ioInstance = (IUnknown*)wrapperMetaData->GetApplicationHandle();
        return true;
    }
}