//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file   DX12ImageRenderer.cpp
/// \brief  Implementation file for DX12ImageRenderer.
///         This class helps to render D3D12 resources into RGBA8 CPU buffers.
//==============================================================================

#include "DX12ImageRenderer.h"

#include <d3dcompiler.h>
#include <png.h>
#include <fstream>
#include "../Util/d3dx12.h"

/**
***************************************************************************************************
*   DX12ImageRenderer::Create
*
*   @brief
*       Statically create a DX12ImageRenderer.
*
*   @return
*       A DX12ImageRenderer pointer, or NULL if init fails.
***************************************************************************************************
*/
DX12ImageRenderer* DX12ImageRenderer::Create(const DX12ImageRendererConfig& config)
{
    DX12ImageRenderer* pOut = new DX12ImageRenderer();

    if (pOut != nullptr)
    {
        if (pOut->Init(config) != S_OK)
        {
            delete pOut;
            pOut = nullptr;
        }
    }

    return pOut;
}

/**
***************************************************************************************************
*   DX12ImageRenderer::DX12ImageRenderer
*
*   @brief
*       Constructor.
***************************************************************************************************
*/
DX12ImageRenderer::DX12ImageRenderer() :
    m_pCmdAllocator(nullptr),
    m_pCmdList(nullptr),
    m_pRootSignatureGraphics(nullptr),
    m_pPipelineStateGraphics(nullptr),
    m_pSrvUavCbHeap(nullptr),
    m_pFence(nullptr),
    m_pPSWriteBuf(nullptr),
    m_pPSWriteBufReadBack(nullptr),
    m_pConstantBuffer(nullptr),
    m_pCbvDataBegin(nullptr),
    m_pInternalRT(nullptr),
    m_pInternalRtvHeap(nullptr),
    m_fenceEvent(0),
    m_fenceValue(0),
    m_srvUavCbDescriptorSize(0)
{
    ZeroMemory(&m_constantBufferData, sizeof(m_constantBufferData));
}

/**
***************************************************************************************************
*   DX12ImageRenderer::~DX12ImageRenderer
*
*   @brief
*       Destructor.
***************************************************************************************************
*/
DX12ImageRenderer::~DX12ImageRenderer()
{
    SAFE_DX_RELEASE(m_pCmdAllocator);
    SAFE_DX_RELEASE(m_pCmdList);
    SAFE_DX_RELEASE(m_pRootSignatureGraphics);
    SAFE_DX_RELEASE(m_pPipelineStateGraphics);
    SAFE_DX_RELEASE(m_pSrvUavCbHeap);
    SAFE_DX_RELEASE(m_pFence);
    SAFE_DX_RELEASE(m_pConstantBuffer);
    SAFE_DX_RELEASE(m_pInternalRtvHeap);
}

/**
***************************************************************************************************
*   DX12ImageRenderer::Init
*
*   @brief
*       Initialize all members needed by this rendering class.
*
*   @return
*       S_OK if successful.
***************************************************************************************************
*/
HRESULT DX12ImageRenderer::Init(const DX12ImageRendererConfig& config)
{
    memcpy(&m_config, &config, sizeof(m_config));

    m_srvUavCbDescriptorSize = m_config.pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create command allocator
    HRESULT result = m_config.pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCmdAllocator));

    // Create command list
    if (result == S_OK)
    {
        result = m_config.pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCmdAllocator, nullptr, IID_PPV_ARGS(&m_pCmdList));
    }

    // Create descriptor heap for RTV
    if (result == S_OK)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        result = m_config.pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pInternalRtvHeap));
    }

    // Create descriptor heap for SRV, UAV, and CBV
    if (result == S_OK)
    {
        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
        srvUavHeapDesc.NumDescriptors = RootParametersCount;
        srvUavHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvUavHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        result = m_config.pDevice->CreateDescriptorHeap(&srvUavHeapDesc, IID_PPV_ARGS(&m_pSrvUavCbHeap));
    }

    // Create synchronization objects
    if (result == S_OK)
    {
        result = m_config.pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    // Create root signature
    if (result == S_OK)
    {
        CD3DX12_DESCRIPTOR_RANGE rangesGfx[RootParametersCount];
        rangesGfx[RootParameterSRV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        rangesGfx[RootParameterUAV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        rangesGfx[RootParameterCBV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

        CD3DX12_ROOT_PARAMETER rootParamsGfx[RootParametersCount];
        rootParamsGfx[RootParameterSRV].InitAsDescriptorTable(1, &rangesGfx[RootParameterSRV], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParamsGfx[RootParameterUAV].InitAsDescriptorTable(1, &rangesGfx[RootParameterUAV], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParamsGfx[RootParameterCBV].InitAsDescriptorTable(1, &rangesGfx[RootParameterCBV], D3D12_SHADER_VISIBILITY_ALL);

        D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDescGfx;
        rootSigDescGfx.Init(3, rootParamsGfx, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ID3DBlob* pSigBlobGfx = nullptr;
        ID3DBlob* pSigBlobGfxErr = nullptr;
        result = D3D12SerializeRootSignature(&rootSigDescGfx, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlobGfx, &pSigBlobGfxErr);
        result = m_config.pDevice->CreateRootSignature(0, pSigBlobGfx->GetBufferPointer(), pSigBlobGfx->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignatureGraphics));
    }

    // Create pipeline state
    if (result == S_OK)
    {
        const std::string shaderSrc =
#include "FsQuadToBuffer.hlsl"
            ;

        size_t fileSize = strlen(shaderSrc.c_str());

        ID3DBlob* pVertexShader = nullptr;
        ID3DBlob* pVsError = nullptr;
        result = D3DCompile(shaderSrc.c_str(), fileSize, NULL, NULL, NULL, "VsMain", "vs_5_0", 0, 0, &pVertexShader, &pVsError);
        if (pVsError)
        {
            OutputDebugStringA((char*)pVsError->GetBufferPointer());
            pVsError->Release();
        }

        ID3DBlob* pPixelShader = nullptr;
        ID3DBlob* pPsError = nullptr;
        result = D3DCompile(shaderSrc.c_str(), fileSize, NULL, NULL, NULL, "PsMain", "ps_5_0", 0, 0, &pPixelShader, &pPsError);
        if (pPsError)
        {
            OutputDebugStringA((char*)pPsError->GetBufferPointer());
            pPsError->Release();
        }

        if (result == S_OK)
        {
            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.VS = { reinterpret_cast<UINT8*>(pVertexShader->GetBufferPointer()), pVertexShader->GetBufferSize() };
            psoDesc.PS = { reinterpret_cast<UINT8*>(pPixelShader->GetBufferPointer()), pPixelShader->GetBufferSize() };
            psoDesc.pRootSignature                  = m_pRootSignatureGraphics;
            psoDesc.RasterizerState                 = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState                      = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable   = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask                      = UINT_MAX;
            psoDesc.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets                = 1;
            psoDesc.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count                = 1;
            result = m_config.pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineStateGraphics));
        }
    }

    // Create constant buffer
    if (result == S_OK)
    {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(ConstBufSize);

        result = m_config.pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_pConstantBuffer));
    }

    if (result == S_OK)
    {
        // Command lists are created in the recording state, but there is nothing
        // to record yet. The main loop expects it to be closed, so close it now.
        result = m_pCmdList->Close();
    }

    return result;
}

/**
***************************************************************************************************
*   DX12ImageRenderer::CaptureImage
*
*   @brief
*       Convert a DX12 resource to a CPU-visible linear buffer of pixels. The data is filled
*       in a user-provided CpuImage struct.
*
*       IMPORTANT: Memory inside pImgOut is allocated on behalf of the caller, so it is their
*       responsibility to free it.
*
*   @return
*       S_OK if successful.
***************************************************************************************************
*/
HRESULT DX12ImageRenderer::CaptureImage(
    ID3D12Resource*       pRes,
    D3D12_RESOURCE_STATES prevState,
    UINT                  newWidth,
    UINT                  newHeight,
    CpuImage*             pImgOut,
    bool                  bFlipX,
    bool                  bFlipY)
{
    HRESULT result = E_FAIL;

    if ((pRes != nullptr) && (pImgOut != nullptr) && (newWidth > 0) && (newHeight > 0))
    {
        // Create temp assets
        result = CreateCaptureAssets(pRes, newWidth, newHeight);

        if (result == S_OK)
        {
            result = m_pCmdAllocator->Reset();
        }

        if (result == S_OK)
        {
            result = m_pCmdList->Reset(m_pCmdAllocator, m_pPipelineStateGraphics);
        }

        // Render work
        if (result == S_OK)
        {
            // Set root sig
            m_pCmdList->SetGraphicsRootSignature(m_pRootSignatureGraphics);

            // Set descriptors and tables
            ID3D12DescriptorHeap* ppHeaps[] = { m_pSrvUavCbHeap };
            m_pCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_pSrvUavCbHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterSRV, m_srvUavCbDescriptorSize);
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_pSrvUavCbHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterUAV, m_srvUavCbDescriptorSize);
            CD3DX12_GPU_DESCRIPTOR_HANDLE cbHandle(m_pSrvUavCbHeap->GetGPUDescriptorHandleForHeapStart(), RootParameterCBV, m_srvUavCbDescriptorSize);

            m_pCmdList->SetGraphicsRootDescriptorTable(RootParameterSRV, srvHandle);
            m_pCmdList->SetGraphicsRootDescriptorTable(RootParameterUAV, uavHandle);
            m_pCmdList->SetGraphicsRootDescriptorTable(RootParameterCBV, cbHandle);

            // Set viewport
            D3D12_VIEWPORT viewport = {};
            viewport.Width    = static_cast<float>(newWidth);
            viewport.Height   = static_cast<float>(newHeight);
            viewport.MaxDepth = 1.0f;
            m_pCmdList->RSSetViewports(1, &viewport);

            // Set scissor
            D3D12_RECT scissorRect = {};
            scissorRect.right  = static_cast<LONG>(newWidth);
            scissorRect.bottom = static_cast<LONG>(newHeight);
            m_pCmdList->RSSetScissorRects(1, &scissorRect);

            // Update const buf
            m_constantBufferData.rtWidth = newWidth;
            m_constantBufferData.flipX   = bFlipX ? 1 : 0;
            m_constantBufferData.flipY   = bFlipY ? 1 : 0;
            CD3DX12_RANGE readRange(0, 0);
            result = m_pConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin));
            memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
            m_pConstantBuffer->Unmap(0, nullptr);

            // Set RT
            CD3DX12_CPU_DESCRIPTOR_HANDLE internalRtvHandle(m_pInternalRtvHeap->GetCPUDescriptorHandleForHeapStart());
            m_config.pDevice->CreateRenderTargetView(m_pInternalRT, nullptr, internalRtvHandle);
            m_pCmdList->OMSetRenderTargets(1, &internalRtvHandle, FALSE, nullptr);

            // Record commands
            m_pCmdList->ClearRenderTargetView(internalRtvHandle, ClearColor, 0, nullptr);
            m_pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            CD3DX12_RESOURCE_BARRIER barrier = {};

            // Render full screen quad and write out UAV
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRes, prevState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_pCmdList->ResourceBarrier(1, &barrier);

            m_pCmdList->DrawInstanced(3, 1, 0, 0);

#if OVERWRITE_SRC_RES
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            m_pCmdList->ResourceBarrier(1, &barrier);
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pInternalRT, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            m_pCmdList->ResourceBarrier(1, &barrier);
            m_pCmdList->CopyResource(pRes, m_pInternalRT);
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pInternalRT, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_pCmdList->ResourceBarrier(1, &barrier);
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRes, D3D12_RESOURCE_STATE_COPY_DEST, prevState);
            m_pCmdList->ResourceBarrier(1, &barrier);
#endif

            // Copy UAV to CPU-visible buffer
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pPSWriteBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            m_pCmdList->ResourceBarrier(1, &barrier);
            m_pCmdList->CopyResource(m_pPSWriteBufReadBack, m_pPSWriteBuf);
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pPSWriteBuf, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_pCmdList->ResourceBarrier(1, &barrier);

            // Execute the command list
            result = m_pCmdList->Close();
            ID3D12CommandList* ppCommandLists[] = { m_pCmdList };
            m_config.pCmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            WaitCmdListFinish();

            // Read back UAV results
            void* pUavData = nullptr;
            result = m_pPSWriteBufReadBack->Map(0, &readRange, &pUavData);

            if (result == S_OK)
            {
                const UINT totalBytes = newWidth * newHeight * BytesPerPixel;

                pImgOut->pitch  = newWidth * BytesPerPixel;
                pImgOut->width  = newWidth;
                pImgOut->height = newHeight;
                pImgOut->pData  = new char[totalBytes];

                memcpy(pImgOut->pData, pUavData, totalBytes);

                m_pPSWriteBufReadBack->Unmap(0, nullptr);
            }
        }

        // Free temp assets
        FreeCaptureAssets();
    }

    return result;
}

/**
***************************************************************************************************
*   DX12ImageRenderer::CreateCaptureAssets
*
*   @brief
*       Create resources that are unique to each capture.
*
*   @return
*       S_OK if successful.
***************************************************************************************************
*/
HRESULT DX12ImageRenderer::CreateCaptureAssets(ID3D12Resource* pRes, UINT newWidth, UINT newHeight)
{
    HRESULT result = E_FAIL;

    // Create internal RT
    D3D12_RESOURCE_DESC resDesc = pRes->GetDesc();
    resDesc.Width  = newWidth;
    resDesc.Height = newHeight;

    const UINT bufferByteSize = newWidth * newHeight * BytesPerPixel;
    const UINT bufferNumElements = bufferByteSize / BytesPerPixel;

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy(&clearVal.Color[0], &ClearColor[0], sizeof(ClearColor));

    // Create internal RT
    result = m_config.pDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearVal,
        IID_PPV_ARGS(&m_pInternalRT));

    // Create UAV resources
    if (result == S_OK)
    {
        D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferByteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Create buffer on device memory
        result = m_config.pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_pPSWriteBuf));

        // Create buffer on system memory
        resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferByteSize);
        heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        result = m_config.pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_pPSWriteBufReadBack));
    }

    // Create views
    if (result == S_OK)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE commandsSrvHandle(m_pSrvUavCbHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterSRV, m_srvUavCbDescriptorSize);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels     = 1;
        m_config.pDevice->CreateShaderResourceView(pRes, &srvDesc, commandsSrvHandle);

        CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandleUav(m_pSrvUavCbHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterUAV, m_srvUavCbDescriptorSize);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format                     = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.NumElements         = bufferNumElements;
        uavDesc.Buffer.StructureByteStride = BytesPerPixel;
        m_config.pDevice->CreateUnorderedAccessView(m_pPSWriteBuf, nullptr, &uavDesc, commandsHandleUav);

        CD3DX12_CPU_DESCRIPTOR_HANDLE commandsHandleCbv(m_pSrvUavCbHeap->GetCPUDescriptorHandleForHeapStart(), RootParameterCBV, m_srvUavCbDescriptorSize);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_pConstantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes    = (sizeof(ConstantBuffer) + 255) & ~255;	// CB size is required to be 256-byte aligned.
        m_config.pDevice->CreateConstantBufferView(&cbvDesc, commandsHandleCbv);
    }

    return result;
}

/**
***************************************************************************************************
*   DX12ImageRenderer::FreeCaptureAssets
*
*   @brief
*       Destroy per-capture resources.
***************************************************************************************************
*/
void DX12ImageRenderer::FreeCaptureAssets()
{
    SAFE_DX_RELEASE(m_pInternalRT);
    SAFE_DX_RELEASE(m_pPSWriteBuf);
    SAFE_DX_RELEASE(m_pPSWriteBufReadBack);
}

/**
***************************************************************************************************
*   DX12ImageRenderer::WaitCmdListFinish
*
*   @brief
*       Wait for a command list to finish.
***************************************************************************************************
*/
void DX12ImageRenderer::WaitCmdListFinish()
{
    // Signal and increment the fence value
    const UINT64 fence = m_fenceValue;
    HRESULT result = m_config.pCmdQueue->Signal(m_pFence, fence);
    m_fenceValue++;

    const UINT64 fenceCompletedVal = m_pFence->GetCompletedValue();

    // Wait until the previous frame is finished
    if (fenceCompletedVal < fence)
    {
        result = m_pFence->SetEventOnCompletion(fence, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

/**
***************************************************************************************************
*   CustomPngWriteCb
*
*   @brief
*       Callback function used in PNG dumps.
***************************************************************************************************
*/
void CustomPngWriteCb(png_structp png_ptr, png_bytep data, png_size_t length)
{
    std::vector<UCHAR>* pVec = (std::vector<UCHAR>*)png_get_io_ptr(png_ptr);
    pVec->insert(pVec->end(), data, data + length);
}

/**
***************************************************************************************************
*   DX12ImageRenderer::CpuImageToPng
*
*   @brief
*       Take an RGBA CpuImage ptr and put it in PNG form resident in system memory.
*
*       IMPORTANT: Memory inside ppImage is allocated on behalf of the caller, so it is their
*       responsibility to free it.
*
*   @return
*       True if successful.
***************************************************************************************************
*/
bool DX12ImageRenderer::CpuImageToPng(
    CpuImage*          pImage,
    UCHAR**            ppPngMem,
    UINT*              pMemSize,
    const std::string& fileName) // Optional
{
    bool success = false;

    if ((pImage != nullptr) && (ppPngMem != nullptr) && (pMemSize != nullptr))
    {
        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (png_ptr)
        {
            png_infop info_ptr = png_create_info_struct(png_ptr);
            png_set_IHDR(png_ptr, info_ptr, pImage->width, pImage->height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

            std::vector<UCHAR*> rows(pImage->height);
            for (UINT i = 0; i < pImage->height; i++)
            {
                rows[i] = (UCHAR*)pImage->pData + i * pImage->width * 4;
            }

            png_set_rows(png_ptr, info_ptr, &rows[0]);

            std::vector<UCHAR> pngData;
            png_set_write_fn(png_ptr, &pngData, CustomPngWriteCb, NULL);

            png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
            png_destroy_write_struct(&png_ptr, NULL);

            const UINT memSize = (UINT)pngData.size();

            if (memSize != 0)
            {
                UCHAR* pOut = new UCHAR[memSize];

                if (pOut != nullptr)
                {
                    memcpy(pOut, &pngData[0], memSize);

                    *ppPngMem = pOut;
                    *pMemSize = memSize;

                    success = true;

                    // Optionally, dump it out to file
                    if (fileName != "")
                    {
                        std::ofstream outfile(fileName.c_str(), std::ios::out | std::ios::binary);
                        outfile.write((const char*)pOut, memSize);
                        outfile.close();
                    }
                }
            }
        }


    }

    return success;
}