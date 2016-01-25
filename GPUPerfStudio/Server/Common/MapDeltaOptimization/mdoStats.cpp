//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  Stats implementation file for MDO.
///         A simple class used to measure MDO performance and mem consumption.
//==============================================================================

#include "mdoStats.h"

static MdoStats* g_pInstance = nullptr;

/**
**************************************************************************************************
*   Static methods exposed to caller.
**************************************************************************************************
*/
MdoStats* MdoStats::Create(const MdoConfig& mdoConfig)
{
    MdoStats* pOut = g_pInstance;

    if (g_pInstance == nullptr)
    {
        g_pInstance = new MdoStats(mdoConfig);

        if (g_pInstance != nullptr)
        {
            pOut = g_pInstance;
        }
    }
    else
    {
        pOut = nullptr;
    }

    return pOut;
}

void MdoStats::TrackHeapAlloc(UINT32 numBytes, void* pMem)
{
    if (g_pInstance != nullptr) { g_pInstance->TrackAlloc(numBytes, pMem); }
}

void MdoStats::TrackHeapFree(void* pMem)
{
    if (g_pInstance != nullptr) { g_pInstance->TrackFree(pMem); }
}

void MdoStats::StartCpuTimer()
{
    if (g_pInstance != nullptr) { g_pInstance->StartTimer(); }
}

void MdoStats::StopCpuTimer(const std::string& desc)
{
    if (g_pInstance != nullptr) { g_pInstance->StopTimer(desc); }
}

/**
**************************************************************************************************
*   MdoStats::MdoStats
*
*   @brief
*       Create MdoStats object
**************************************************************************************************
*/
MdoStats::MdoStats(const MdoConfig& mdoConfig) : m_totalConsumption(0)
{
    memcpy(&m_mdoConfig, &mdoConfig, sizeof(mdoConfig));
}

/**
**************************************************************************************************
*   MdoStats::TrackAlloc
*
*   @brief
*       Track when heap memory has been allocated
**************************************************************************************************
*/
void MdoStats::TrackAlloc(UINT32 numBytes, void* pMem)
{
    if (m_mdoConfig.dbgMdoSpaceUsage == true)
    {
        m_allocSizes[pMem] = numBytes;

        m_totalConsumption += numBytes;

        std::stringstream s;
        s << "Alloc(" << numBytes << ") ... Running total: " << MdoUtil::FormatWithCommas(m_totalConsumption) << " bytes";

        MdoUtil::PrintLn(s.str().c_str());
    }
}

/**
**************************************************************************************************
*   MdoStats::TrackFree
*
*   @brief
*       Track when heap memory has been freed
**************************************************************************************************
*/
void MdoStats::TrackFree(void* pMem)
{
    if (m_mdoConfig.dbgMdoSpaceUsage == true)
    {
        if (m_allocSizes.count(pMem) > 0)
        {
            UINT32 freedBytes = m_allocSizes[pMem];

            m_totalConsumption -= freedBytes;

            m_allocSizes.erase(pMem);

            std::stringstream s;
            s << "Free(" << freedBytes << ") ... Running total: " << MdoUtil::FormatWithCommas(m_totalConsumption) << " bytes";

            MdoUtil::PrintLn(s.str().c_str());
        }
    }
}

/**
**************************************************************************************************
*   MdoStats::StartTimer
*
*   @brief
*       Start a CPU timer
**************************************************************************************************
*/
void MdoStats::StartTimer()
{
    if (m_mdoConfig.dbgMdoTimeUsage == true)
    {
        m_timer.Start();
    }
}

/**
**************************************************************************************************
*   MdoStats::StopTimer
*
*   @brief
*       Stop the CPU timer and output the time
*
*   @return
*       The time taken since the last StartTimer()
**************************************************************************************************
*/
double MdoStats::StopTimer(const std::string& desc)
{
    double time = 0;

    if (m_mdoConfig.dbgMdoTimeUsage == true)
    {
        time = m_timer.Stop() * 1000;

        std::stringstream s;
        s << desc << " ... " << time << " ms";

        MdoUtil::PrintLn(s.str().c_str());
    }

    return time;
}
