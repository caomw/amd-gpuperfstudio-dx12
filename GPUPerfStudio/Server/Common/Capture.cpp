//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  TODO add documentation
//==============================================================================
 
#if defined (_WIN32)
    #include <windows.h>
#elif defined (_LINUX)
    #include "WinDefs.h"
#endif
#include "Capture.h"

bool CaptureOverride::Do(Capture* pCap)
{
    return pCap->Play();
};
