#include "pch.h"

#include "Editor/Common/Backend/ImGuiRenderer.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Window/IWindow.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiRendererInternal
        {
            /** @brief 셰이더 — 에디터 전용이라 에디터 도메인에 둔다. 정점 배치가 아래 static_assert 와 묶여 있어 데이터로 바꿔 끼우지 않는다. */
            static constexpr const utf8* kShaderPath = "editor/shaders/imgui.hlsl";
            /** @brief ImDrawVert 하나가 차지하는 uint 칸 수입니다(셰이더 `kImGuiVertexWords`). */
            static constexpr uint32 kVertexWords = 5u;
            /** @brief 루트 상수 칸 — 셰이더 `SW_ROOT_CONSTANTS_BEGIN` 블록의 순서와 같다. */
            enum RootConstant : uint32
            {
                kRootVertexBase = 0,
                kRootIndexBase,
                kRootTexture,
                kRootScaleX,
                kRootScaleY,
                kRootOffsetX,
                kRootOffsetY,
                kRootConstantCount,
            };
            /** @brief 버퍼가 모자라면 이 배율로 키운다(창이 커지며 프레임마다 조금씩 늘 때 다시 만들기를 줄인다). */
            static constexpr float32 kBufferGrowth = 1.5f;
            /**
             * @brief `ImTextureStatus_WantDestroy` 텍스처를 파괴하기 전에 기다릴 ImGui 프레임 수입니다.
             * @details 렌더 스레드가 그리는 스냅샷(최대 `kMaxFrameCountInFlight` 장)이 옛 아틀라스를 아직 그릴 수 있다. GPU 사용은 백엔드의 해제(번호 · 텍스처)가 기다린다.
             */
            static constexpr int32 kTextureDestroyDelayFrames = static_cast<int32>( constant::render::kMaxFrameCountInFlight + 1u );

            static_assert( sizeof( ImDrawVert ) == kVertexWords * sizeof( uint32 ), "ImDrawVert must be pos float2 + uv float2 + col uint (imgui.hlsl)" );

            /** @brief ImGui 텍스처 ID ↔ 칸 번호(0 은 ImGui 의 무효 ID 라 하나를 더한다). */
            static ImTextureID makeTextureID( uint32 slotIndex ) { return static_cast<ImTextureID>( slotIndex ) + 1u; }
            static bool        tryGetSlotIndex( uint64 textureID, uint32& outSlotIndex )
            {
                if ( textureID == 0u )
                    return false;
                outSlotIndex = static_cast<uint32>( textureID - 1u );
                return true;
            }

            static ImGuiRenderer* findRenderer() { return ImGui::GetCurrentContext() != nullptr ? static_cast<ImGuiRenderer*>( ImGui::GetIO().BackendRendererUserData ) : nullptr; }

            /**
             * @brief 감시 값 `Editor.PlatformWindows` — 스왑체인을 단 보조 창(메인 밖의 OS 창) 가운데 패널 창을 띄운 것의 수입니다. 렌더러가 없으면 값이 없습니다.
             * @details 툴팁 · 팝업도 메인 창 가장자리에서는 제 OS 창이 된다(가상 커서가 가장자리 근처면) — 패널을 띄우고 닫는 시나리오가 그것까지 세지 않게 뺀다.
             */
            [[nodiscard]] static bool readPlatformWindows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( findRenderer() == nullptr )
                    return false;
                uint32 count{ 0 };
                for ( int32 index = 1; index < ImGui::GetPlatformIO().Viewports.Size; ++index )
                {
                    const ImGuiViewportP* const pViewport    = static_cast<const ImGuiViewportP*>( ImGui::GetPlatformIO().Viewports[index] );
                    const bool                  bPanelWindow = pViewport->RendererUserData != nullptr && pViewport->Window != nullptr &&
                                              ( pViewport->Window->Flags & ( ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup ) ) == 0;
                    count += bPanelWindow ? 1u : 0u;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
            /** @brief 감시 값 `Editor.PlatformWindowFrames` — 보조 창에 그린 누적 횟수입니다. */
            [[nodiscard]] static bool readPlatformWindowFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ImGuiRenderer* const pRenderer = findRenderer();
                if ( pRenderer == nullptr )
                    return false;
                outValue = static_cast<float64>( pRenderer->getPlatformWindowFrames() );
                return true;
            }

            /** @brief 보조 창의 스왑체인 크기(픽셀)입니다. 0 이면 1 로 둔다 — 스왑체인은 0 크기로 만들 수 없다. */
            static uint32 toPixels( float32 size, float32 scale ) { return MathUtil::max( 1u, static_cast<uint32>( size * ( scale > 0.0f ? scale : 1.0f ) ) ); }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorPlatformWindows, "Editor.PlatformWindows", "Editor floating windows (OS windows outside the main window) that have an RHI swap chain",
                         &ImGuiRendererInternal::readPlatformWindows );
    SW_AUTOMATION_WATCH( editorPlatformWindowFrames, "Editor.PlatformWindowFrames", "Times the editor drew a floating window into its own swap chain (cumulative)",
                         &ImGuiRendererInternal::readPlatformWindowFrames );
} // namespace sw::editor

namespace sw::editor
{
    ImGuiRenderer::ImGuiRenderer()
        : _pDevice{ nullptr }
        , _pso{ 0 }
        , _wordBuffer{}
        , _listWord{}
        , _mapListBase{}
        , _listFrameSRV{}
        , _bNativeBindless{ false }
    {
    }

    ImGuiRenderer::~ImGuiRenderer() = default;

    bool ImGuiRenderer::initialize( IRHIDevice* pRHIDevice )
    {
        using Internal = ImGuiRendererInternal;
        if ( pRHIDevice == nullptr || pRHIDevice->getResourceFactory() == nullptr )
            return false;
        _pDevice         = pRHIDevice;
        _bNativeBindless = pRHIDevice->supportsNativeBindlessSampling();

        // 곧은 알파(ImGui 의 규약 — 글꼴 아틀라스 · 색 모두 곱하지 않은 알파다), 깊이 · 컬링 없음. 캡처 텍스처와 보조 창도 백버퍼 포맷이라 PSO 는 하나다.
        RHIPipelineStateDesc desc{};
        desc._vertexShaderPath    = Internal::kShaderPath;
        desc._pixelShaderPath     = Internal::kShaderPath;
        desc._vertexEntryPoint    = "VSMain";
        desc._pixelEntryPoint     = "PSMain";
        desc._numRenderTargets    = 1;
        desc._arrRTVFormat[0]     = pRHIDevice->getBackBufferFormat();
        desc._depthStencilFormat  = RHIFormat::Unknown;
        desc._bEnableDepthTest    = 0;
        desc._bEnableDepthWrite   = 0;
        desc._bEnableBlend        = 1;
        desc._bPremultipliedAlpha = SW_FALSE;
        desc._cullMode            = RHICullMode::None;
        _pso                      = pRHIDevice->getResourceFactory()->createPipelineState( desc );
        if ( _pso == 0 )
        {
            SW_LOG_ERROR_ID( "ImGuiRenderer.PSOFailed", "The editor UI pipeline (%#) could not be created - see the shader error above", Internal::kShaderPath );
            _pDevice = nullptr;
            return false;
        }

        ImGuiIO& io                = ImGui::GetIO();
        io.BackendRendererUserData = this;
        io.BackendRendererName     = "sw_rhi";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports;

        // 보조 창은 RHI 스왑체인이다. 그리기는 렌더 스레드가 한다(`renderPlatformWindows`) — ImGui 의 RenderPlatformWindowsDefault 를 부르지 않으므로 그리기 · 표시 훅은 비운다.
        ImGuiPlatformIO& platformIO       = ImGui::GetPlatformIO();
        platformIO.Renderer_CreateWindow  = &ImGuiRenderer::createWindowSwapChain;
        platformIO.Renderer_DestroyWindow = &ImGuiRenderer::destroyWindowSwapChain;
        platformIO.Renderer_SetWindowSize = &ImGuiRenderer::resizeWindowSwapChain;
        platformIO.Renderer_RenderWindow  = nullptr;
        platformIO.Renderer_SwapBuffers   = nullptr;
        return true;
    }

    void ImGuiRenderer::shutdown()
    {
        if ( _pDevice == nullptr )
            return;
        // 보조 창의 스왑체인을 먼저 부순다(창보다 표면이 먼저 사라져야 한다) — ImGui 가 창마다 Renderer_DestroyWindow 를 부른다.
        if ( ImGui::GetCurrentContext() != nullptr )
            ImGui::DestroyPlatformWindows();

        // 렌더 스레드 · GPU 를 기다려 디바이스로 넘긴 해제를 모두 실행하고, 넘기지 않은 해제도 지금 실행한다(콜백이 이 렌더러를 가리킨다).
        _pDevice->waitIdle( RHIWaitIdleReason::Shutdown );
        _drawReleaseQueue.flushAll();

        if ( ImGui::GetCurrentContext() != nullptr )
        {
            for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
            {
                if ( pTexture != nullptr && pTexture->Status != ImTextureStatus_Destroyed )
                {
                    pTexture->SetTexID( ImTextureID_Invalid );
                    pTexture->SetStatus( ImTextureStatus_Destroyed );
                }
            }
        }
        uint32 slotCount{ 0 };
        {
            ScopedLock<mutex> lock{ _textureMutex };
            slotCount = static_cast<uint32>( _listTextureSlot.size() );
        }
        for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
        {
            releaseSlot( *_pDevice, slotIndex ); // GPU 가 비었다 — 바로 놓는다
        }
        {
            ScopedLock<mutex> lock{ _textureMutex };
            _listTextureSlot.clear();
            _listFreeSlot.clear();
        }
        _mapForeignSRV.clear();
        _wordBuffer.release( _pDevice );
        if ( _pso != 0 && _pDevice->getResourceFactory() != nullptr )
            _pDevice->getResourceFactory()->destroyPipelineState( _pso );
        _pso = 0;

        if ( ImGui::GetCurrentContext() != nullptr )
        {
            ImGuiIO& io                = ImGui::GetIO();
            io.BackendRendererUserData = nullptr;
            io.BackendRendererName     = nullptr;
            io.BackendFlags &= ~( ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports );
            ImGuiPlatformIO& platformIO       = ImGui::GetPlatformIO();
            platformIO.Renderer_CreateWindow  = nullptr;
            platformIO.Renderer_DestroyWindow = nullptr;
            platformIO.Renderer_SetWindowSize = nullptr;
        }
        _pDevice = nullptr;
    }

    void ImGuiRenderer::prepareFrame( IRHIDevice& device, const vector<ImDrawData*>& listDrawData )
    {
        using Internal = ImGuiRendererInternal;
        // 1) ImGui 동적 텍스처(글꼴 아틀라스) — 그리기 전에 만들고 갱신한다. 그리기 명령은 그리는 순간 텍스처의 ID 를 읽는다.
        for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
        {
            if ( pTexture != nullptr )
                updateTexture( device, *pTexture );
        }

        // 2) 칸의 bindless 번호 — 패스 기록 밖(present 훅)이라 등록할 수 있다. 업로드가 끝나지 않은 텍스처는 등록이 거절되어 다음 프레임에 다시 본다.
        {
            ScopedLock<mutex> lock{ _textureMutex };
            _listFrameSRV.resize( _listTextureSlot.size() );
            for ( size_t slotIndex = 0; slotIndex < _listTextureSlot.size(); ++slotIndex )
            {
                TextureSlot& slot = _listTextureSlot[slotIndex];
                if ( slot._bInUse && slot._srv == kInvalidDescriptorIndex && slot._texture != 0 )
                {
                    // 패널 텍스처는 텍스처마다 한 번만 등록한다 — 번호는 텍스처의 파괴가 놓으므로, 다시 열 때마다 등록하면 텍스처가 사는 동안 번호가 쌓인다.
                    const auto it = _mapForeignSRV.find( slot._texture );
                    if ( it != _mapForeignSRV.end() )
                        slot._srv = it->second;
                    else
                    {
                        slot._srv = device.getResourceFactory()->registerBindlessTexture( slot._texture );
                        if ( slot._srv != kInvalidDescriptorIndex )
                            _mapForeignSRV[slot._texture] = slot._srv;
                    }
                }
                _listFrameSRV[slotIndex] = slot._bInUse ? slot._srv : kInvalidDescriptorIndex;
            }
        }

        // 3) 그리기 목록 — 정점(uint 다섯) 다음에 인덱스(uint 하나, ImDrawIdx 를 넓힌다)를 목록마다 잇는다.
        _listWord.clear();
        _mapListBase.clear();
        for ( const ImDrawData* pDrawData : listDrawData )
        {
            if ( pDrawData == nullptr )
                continue;
            for ( const ImDrawList* pList : pDrawData->CmdLists )
            {
                ListBase base{};
                base._vertexWord             = static_cast<uint32>( _listWord.size() );
                const size_t vertexWordCount = static_cast<size_t>( pList->VtxBuffer.Size ) * Internal::kVertexWords;
                _listWord.resize( _listWord.size() + vertexWordCount );
                Memory::copy( _listWord.data() + base._vertexWord, pList->VtxBuffer.Data, vertexWordCount * sizeof( uint32 ) );
                base._indexWord = static_cast<uint32>( _listWord.size() );
                for ( const ImDrawIdx index : pList->IdxBuffer )
                {
                    _listWord.push_back( static_cast<uint32>( index ) );
                }
                _mapListBase[pList] = base;
            }
        }
        if ( _listWord.empty() )
            return;
        const uint32 wordCount = static_cast<uint32>( _listWord.size() );
        if ( _wordBuffer.isValid() == false || _wordBuffer._capacityElements < wordCount )
        {
            const uint32 capacity = MathUtil::max( wordCount, static_cast<uint32>( static_cast<float32>( _wordBuffer._capacityElements ) * Internal::kBufferGrowth ) );
            _wordBuffer.release( &device );
            if ( _wordBuffer.ensureCapacity( &device, sizeof( uint32 ), capacity, RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
            {
                SW_LOG_ERROR_ONCE_ID( "ImGuiRenderer.BufferFailed", "Failed to create the editor UI buffer (%# words) - the editor UI is not drawn", capacity );
                _mapListBase.clear();
                return;
            }
        }
        _wordBuffer.upload( &device, _listWord.data(), wordCount * static_cast<uint32>( sizeof( uint32 ) ) );
    }

    void ImGuiRenderer::render( IRHIDevice& device, const ImDrawData& drawData )
    {
        using Internal                           = ImGuiRendererInternal;
        IRHICommandContext* const pCmd           = device.getFrameStreamContext();
        const float32             framebufferW   = drawData.DisplaySize.x * drawData.FramebufferScale.x;
        const float32             framebufferH   = drawData.DisplaySize.y * drawData.FramebufferScale.y;
        const bool                bNothingToDraw = pCmd == nullptr || _pso == 0 || _wordBuffer.isValid() == false || framebufferW <= 0.0f || framebufferH <= 0.0f;
        if ( bNothingToDraw )
            return;

        // 정점은 셰이더가 구조 버퍼에서 꺼낸다 — 정점 버퍼 슬롯은 비운다(캔버스와 같다).
        pCmd->setVertexBuffer( 0, 0, 0, 0 );
        pCmd->setVertexBuffer( constant::render::kInstanceSlotStreamSlot, 0, 0, 0 );
        pCmd->setPipelineState( _pso );
        pCmd->bindStructuredBuffer( _wordBuffer._srv, shaderslot::kPassBuffer ); // setPipelineState 가 슬롯 상태를 비운다 — PSO 뒤에 건다

        // 화면 좌표(DisplayPos 기준) → NDC. y 는 아래가 + 다(ImGui 와 RHI 의 화면 규약이 같다 — GL 은 클립 원점으로 맞춘다).
        uint32        arrRoot[Internal::kRootConstantCount]{};
        const float32 scaleX            = 2.0f / drawData.DisplaySize.x;
        const float32 scaleY            = -2.0f / drawData.DisplaySize.y;
        arrRoot[Internal::kRootScaleX]  = BitUtil::floatToBits( scaleX );
        arrRoot[Internal::kRootScaleY]  = BitUtil::floatToBits( scaleY );
        arrRoot[Internal::kRootOffsetX] = BitUtil::floatToBits( -1.0f - drawData.DisplayPos.x * scaleX );
        arrRoot[Internal::kRootOffsetY] = BitUtil::floatToBits( 1.0f - drawData.DisplayPos.y * scaleY );

        for ( const ImDrawList* pList : drawData.CmdLists )
        {
            const auto it = _mapListBase.find( pList );
            if ( it == _mapListBase.end() )
            {
                SW_LOG_ERROR_ONCE_ID( "ImGuiRenderer.ListNotPrepared", "An editor UI draw list was not prepared this frame (prepareFrame) - it is not drawn" );
                continue;
            }
            for ( const ImDrawCmd& cmd : pList->CmdBuffer )
            {
                if ( cmd.UserCallback != nullptr )
                {
                    // 상태를 되돌리라는 표지는 다음 명령이 PSO · 버퍼를 다시 걸지 않아도 된다 — 이 렌더러는 명령마다 루트 상수 · 가위만 바꾼다.
                    if ( cmd.UserCallback != ImDrawCallback_ResetRenderState )
                        cmd.UserCallback( pList, &cmd );
                    continue;
                }
                const float32 clipMinX = MathUtil::max( 0.0f, ( cmd.ClipRect.x - drawData.DisplayPos.x ) * drawData.FramebufferScale.x );
                const float32 clipMinY = MathUtil::max( 0.0f, ( cmd.ClipRect.y - drawData.DisplayPos.y ) * drawData.FramebufferScale.y );
                const float32 clipMaxX = MathUtil::min( framebufferW, ( cmd.ClipRect.z - drawData.DisplayPos.x ) * drawData.FramebufferScale.x );
                const float32 clipMaxY = MathUtil::min( framebufferH, ( cmd.ClipRect.w - drawData.DisplayPos.y ) * drawData.FramebufferScale.y );
                if ( clipMaxX <= clipMinX || clipMaxY <= clipMinY || cmd.ElemCount == 0 )
                    continue;
                const RHIDescriptorIndex srv = resolveTexture( static_cast<uint64>( cmd.GetTexID() ) );
                if ( srv == kInvalidDescriptorIndex )
                    continue; // 아직 등록되지 않았거나(업로드 중) 해제한 텍스처 — 흰색으로 대신 그리지 않는다

                const RHIScissorRect scissor{ static_cast<uint32>( clipMinX ), static_cast<uint32>( clipMinY ), static_cast<uint32>( clipMaxX - clipMinX ),
                                              static_cast<uint32>( clipMaxY - clipMinY ) };
                pCmd->setScissorRect( scissor );
                arrRoot[Internal::kRootVertexBase] = it->second._vertexWord + cmd.VtxOffset * Internal::kVertexWords;
                arrRoot[Internal::kRootIndexBase]  = it->second._indexWord + cmd.IdxOffset;
                if ( _bNativeBindless )
                    arrRoot[Internal::kRootTexture] = srv; // 전역 번호
                else
                {
                    // 에뮬 백엔드(DX11 · GL)는 머티리얼 텍스처 슬롯 t16 에 서수 0 으로 건다 — MaterialTexture::sampleIndexTexture 가 서수로 고른다.
                    pCmd->bindShaderResource( srv, shaderslot::kMaterialTexture0 );
                    arrRoot[Internal::kRootTexture] = 0;
                }
                pCmd->setGraphicsRootConstants( 0, Internal::kRootConstantCount, arrRoot );
                // 삼각형마다 인스턴스 하나(셰이더 VSMain) — 한 드로우가 만드는 정점을 셋으로 둔다(메시 없는 드로우의 슬롯 0 버퍼 크기 안).
                pCmd->drawInstanced( 3, cmd.ElemCount / 3u, 0, 0 );
            }
        }
        // 가위를 대상 전체로 되돌린다(같은 패스에서 뒤에 그리는 것이 있어도 잘리지 않게).
        pCmd->setScissorRect( RHIScissorRect{ 0, 0, static_cast<uint32>( framebufferW ), static_cast<uint32>( framebufferH ) } );
    }

    void ImGuiRenderer::collectPlatformWindowDrawData( vector<ImDrawData*>& inoutListDrawData ) const
    {
        const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        for ( int32 index = 1; index < platformIO.Viewports.Size; ++index ) // 0 은 메인 창이다(스냅샷이 따로 넘긴다)
        {
            ImGuiViewport* const pViewport = platformIO.Viewports[index];
            const bool           bDrawable = ( pViewport->Flags & ImGuiViewportFlags_IsMinimized ) == 0 && pViewport->RendererUserData != nullptr && pViewport->DrawData != nullptr;
            if ( bDrawable )
                inoutListDrawData.push_back( pViewport->DrawData );
        }
    }

    void ImGuiRenderer::renderPlatformWindows( IRHIDevice& device )
    {
        IRHICommandContext* const pCmd       = device.getFrameStreamContext();
        const ImGuiPlatformIO&    platformIO = ImGui::GetPlatformIO();
        if ( pCmd == nullptr )
            return;
        for ( int32 index = 1; index < platformIO.Viewports.Size; ++index )
        {
            ImGuiViewport* const pViewport  = platformIO.Viewports[index];
            IRHISwapChain* const pSwapChain = static_cast<IRHISwapChain*>( pViewport->RendererUserData );
            const bool           bDrawable  = ( pViewport->Flags & ImGuiViewportFlags_IsMinimized ) == 0 && pSwapChain != nullptr && pViewport->DrawData != nullptr;
            if ( bDrawable == false || device.acquireSwapChainImage( *pSwapChain ) == false )
                continue;
            // 열린 패스(메인 백버퍼)를 닫고 이 창의 백버퍼로 연다 — 열린 채로 다른 패스를 열면 Vulkan 이 레이아웃 전이를 서브패스 안에 기록한다.
            pCmd->endRenderPass();
            RHIRenderPassBeginInfo pass{};
            pass._pSwapChain        = pSwapChain;
            pass._bBindColor        = SW_TRUE;
            pass._colorTargetCount  = 1;
            pass._arrColorTarget[0] = 0;
            pass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
            pass._arrClearColor[0]  = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
            pCmd->beginRenderPass( pass );
            render( device, *pViewport->DrawData );
            pCmd->endRenderPass();
            _platformWindowFrames.fetch_add( 1, std::memory_order_relaxed );
        }
    }

    void* ImGuiRenderer::registerTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return nullptr;
        ScopedLock<mutex> lock{ _textureMutex };
        return reinterpret_cast<void*>( static_cast<uintptr_t>( ImGuiRendererInternal::makeTextureID( acquireSlot( texture, false ) ) ) );
    }

    void ImGuiRenderer::unregisterTexture( void* pTextureID )
    {
        uint32 slotIndex{ 0 };
        if ( ImGuiRendererInternal::tryGetSlotIndex( static_cast<uint64>( reinterpret_cast<uintptr_t>( pTextureID ) ), slotIndex ) == false )
            return;
        // 칸은 지금 비운다 — 이 UI 프레임이 낼 스냅샷은 그 ID 를 그리지 않고, 앞 스냅샷은 UI 프레임이 열리기 전에 이미 그려졌다(`waitForDrawSnapshotIdle`).
        if ( _pDevice != nullptr )
            releaseSlot( *_pDevice, slotIndex );
    }

    uint32 ImGuiRenderer::acquireSlot( RHITextureHandle texture, bool bOwned )
    {
        uint32 slotIndex{ 0 };
        if ( _listFreeSlot.empty() == false )
        {
            slotIndex = _listFreeSlot.back();
            _listFreeSlot.pop_back();
        }
        else
        {
            slotIndex = static_cast<uint32>( _listTextureSlot.size() );
            _listTextureSlot.emplace_back();
        }
        TextureSlot& slot = _listTextureSlot[slotIndex];
        slot._texture     = texture;
        slot._srv         = kInvalidDescriptorIndex;
        slot._bInUse      = true;
        slot._bOwned      = bOwned;
        return slotIndex;
    }

    void ImGuiRenderer::releaseSlot( IRHIDevice& device, uint32 slotIndex )
    {
        TextureSlot slot{};
        {
            ScopedLock<mutex> lock{ _textureMutex };
            if ( slotIndex >= _listTextureSlot.size() || _listTextureSlot[slotIndex]._bInUse == false )
                return;
            slot                        = _listTextureSlot[slotIndex];
            _listTextureSlot[slotIndex] = TextureSlot{};
            _listFreeSlot.push_back( slotIndex );
        }
        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        if ( pFactory == nullptr )
            return;
        // 패널이 등록한 텍스처 — 번호는 텍스처 주인의 파괴가 놓는다(IRHIResourceFactory::registerBindlessTexture). 여기서 놓으면 파괴와 순서가 엇갈려 다시 발급된 번호를 놓는다.
        if ( slot._bOwned == false )
            return;
        // 이 렌더러가 만든 텍스처(ImGui 동적 텍스처) — 렌더 스레드의 패스 기록 밖에서 부른다. 번호 · 텍스처 해제는 백엔드가 GPU 사용 뒤로 미룬다.
        if ( slot._srv != kInvalidDescriptorIndex )
            pFactory->unregisterBindlessTexture( slot._srv );
        if ( slot._texture != 0 )
            pFactory->destroyTexture( slot._texture );
    }

    void ImGuiRenderer::updateTexture( IRHIDevice& device, ImTextureData& texture )
    {
        using Internal                      = ImGuiRendererInternal;
        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        if ( texture.Status == ImTextureStatus_OK || texture.Status == ImTextureStatus_Destroyed || pFactory == nullptr )
            return;

        if ( texture.Status == ImTextureStatus_WantDestroy )
        {
            // 그 텍스처를 그렸을 수 있는 스냅샷이 다 지나간 뒤에(ImGui 가 세는 미사용 프레임), 지금 기록 중인 프레임의 GPU 작업이 끝나면 놓는다.
            uint32 slotIndex{ 0 };
            if ( texture.UnusedFrames < Internal::kTextureDestroyDelayFrames )
                return;
            if ( Internal::tryGetSlotIndex( static_cast<uint64>( texture.TexID ), slotIndex ) )
                releaseSlot( device, slotIndex ); // 렌더 스레드 · 패스 기록 밖 — 백엔드가 GPU 사용 뒤로 미룬다
            texture.SetTexID( ImTextureID_Invalid );
            texture.SetStatus( ImTextureStatus_Destroyed );
            return;
        }

        // 아틀라스는 RGBA32 다(ImGui 1.92 기본). Alpha8 이면 흰색 + 알파로 넓힌다 — 셰이더는 RGBA 하나만 읽는다.
        const uint32 width      = static_cast<uint32>( texture.Width );
        const uint32 height     = static_cast<uint32>( texture.Height );
        auto         copyRegion = [&texture]( uint32 x, uint32 y, uint32 regionWidth, uint32 regionHeight, vector<uint8>& outBytes )
        {
            outBytes.resize( static_cast<size_t>( regionWidth ) * regionHeight * 4u );
            for ( uint32 row = 0; row < regionHeight; ++row )
            {
                const uint8* pSource = static_cast<const uint8*>( texture.GetPixelsAt( static_cast<int32>( x ), static_cast<int32>( y + row ) ) );
                uint8*       pTarget = outBytes.data() + static_cast<size_t>( row ) * regionWidth * 4u;
                if ( texture.Format == ImTextureFormat_RGBA32 )
                {
                    Memory::copy( pTarget, pSource, static_cast<size_t>( regionWidth ) * 4u );
                    continue;
                }
                for ( uint32 column = 0; column < regionWidth; ++column )
                {
                    pTarget[column * 4u + 0u] = 255u;
                    pTarget[column * 4u + 1u] = 255u;
                    pTarget[column * 4u + 2u] = 255u;
                    pTarget[column * 4u + 3u] = pSource[column];
                }
            }
        };

        vector<uint8> listByte;
        if ( texture.Status == ImTextureStatus_WantCreate )
        {
            RHITextureDesc desc{};
            desc._width                   = width;
            desc._height                  = height;
            desc._mipLevels               = 1;
            desc._format                  = RHIFormat::R8G8B8A8_UNORM;
            desc._bIsShaderResource       = 1;
            const RHITextureHandle handle = pFactory->createTexture2D( desc );
            copyRegion( 0, 0, width, height, listByte );
            RHITextureUploadDesc upload{};
            upload._pData      = listByte.data();
            upload._sizeBytes  = static_cast<uint32>( listByte.size() );
            upload._mipLevels  = 1;
            upload._arraySlice = 0;
            // 첫 그리기 전에 다 올린다 — 반쯤 올라간 아틀라스로 그린 프레임이 UI 캡처 골든을 흔들지 않게(에디터 전용 · 기동과 아틀라스 확장 때뿐이다).
            if ( handle == 0 || RHITransferUtil::uploadTexture2DAndWait( *pFactory, handle, upload ) == false )
            {
                SW_LOG_ERROR_ID( "ImGuiRenderer.TextureCreateFailed", "Failed to create an editor UI texture (%#x%#) - its glyphs are not drawn", width, height );
                if ( handle != 0 )
                    pFactory->destroyTexture( handle );
                texture.SetStatus( ImTextureStatus_Destroyed );
                return;
            }
            uint32 slotIndex{ 0 };
            {
                ScopedLock<mutex> lock{ _textureMutex };
                slotIndex                        = acquireSlot( handle, true );
                _listTextureSlot[slotIndex]._srv = pFactory->registerBindlessTexture( handle );
            }
            texture.SetTexID( Internal::makeTextureID( slotIndex ) );
            texture.SetStatus( ImTextureStatus_OK );
            return;
        }

        // ImTextureStatus_WantUpdates — 바뀐 구간만 올린다.
        uint32           slotIndex{ 0 };
        RHITextureHandle handle{ 0 };
        if ( Internal::tryGetSlotIndex( static_cast<uint64>( texture.TexID ), slotIndex ) )
        {
            ScopedLock<mutex> lock{ _textureMutex };
            handle = slotIndex < _listTextureSlot.size() ? _listTextureSlot[slotIndex]._texture : 0;
        }
        for ( const ImTextureRect& rect : texture.Updates )
        {
            copyRegion( rect.x, rect.y, rect.w, rect.h, listByte );
            RHITextureRegionUploadDesc region{};
            region._pData     = listByte.data();
            region._sizeBytes = static_cast<uint32>( listByte.size() );
            region._x         = rect.x;
            region._y         = rect.y;
            region._width     = rect.w;
            region._height    = rect.h;
            if ( handle == 0 || RHITransferUtil::uploadTexture2DRegionAndWait( *pFactory, handle, region ) == false )
                SW_LOG_ERROR_ONCE_ID( "ImGuiRenderer.TextureUpdateFailed", "Failed to update an editor UI texture region - new glyphs may be missing" );
        }
        texture.SetStatus( ImTextureStatus_OK );
    }

    RHIDescriptorIndex ImGuiRenderer::resolveTexture( uint64 textureID ) const
    {
        uint32 slotIndex{ 0 };
        if ( ImGuiRendererInternal::tryGetSlotIndex( textureID, slotIndex ) == false || slotIndex >= _listFrameSRV.size() )
            return kInvalidDescriptorIndex;
        return _listFrameSRV[slotIndex];
    }

    void ImGuiRenderer::createWindowSwapChain( ImGuiViewport* pViewport )
    {
        using Internal                 = ImGuiRendererInternal;
        ImGuiRenderer* const pRenderer = Internal::findRenderer();
        if ( pRenderer == nullptr || pRenderer->_pDevice == nullptr || pViewport == nullptr )
            return;
        // 창 핸들은 플랫폼 백엔드가 아는 종류만 읽는다(Win32 HWND · X11 창) — Wayland 보조 창은 표면이 없어 만들지 않는다(다른 핸들로 대신하지 않는다).
        RHISwapChainDesc desc{};
        desc._width  = Internal::toPixels( pViewport->Size.x, pViewport->FramebufferScale.x );
        desc._height = Internal::toPixels( pViewport->Size.y, pViewport->FramebufferScale.y );
#if defined( SW_PLATFORM_WINDOWS )
        desc._surfaceKind   = NativeSurfaceKind::Win32;
        desc._pWindowHandle = IImGuiPlatformBackend::tryGetViewportWin32Handle( pViewport );
#else
        X11SurfaceHandles x11Surface;
        if ( IImGuiPlatformBackend::tryGetViewportX11Surface( pViewport, x11Surface ) )
        {
            desc._surfaceKind    = NativeSurfaceKind::X11;
            desc._pWindowHandle  = reinterpret_cast<void*>( static_cast<uintptr_t>( x11Surface._window ) );
            desc._pWindowDisplay = x11Surface._pDisplay;
        }
#endif
        if ( desc._pWindowHandle == nullptr )
        {
            SW_LOG_ERROR_ID( "ImGuiRenderer.WindowHandleMissing", "An editor floating window has no Win32 or X11 handle - it is not drawn" );
            return;
        }
        pViewport->RendererUserData = pRenderer->_pDevice->createSwapChain( desc ); // 실패는 디바이스가 남긴다 — 그 창은 그리지 않는다
    }

    void ImGuiRenderer::destroyWindowSwapChain( ImGuiViewport* pViewport )
    {
        ImGuiRenderer* const pRenderer = ImGuiRendererInternal::findRenderer();
        if ( pViewport == nullptr || pViewport->RendererUserData == nullptr )
            return;
        if ( pRenderer != nullptr && pRenderer->_pDevice != nullptr )
            pRenderer->_pDevice->destroySwapChain( static_cast<IRHISwapChain*>( pViewport->RendererUserData ) );
        pViewport->RendererUserData = nullptr;
    }

    void ImGuiRenderer::resizeWindowSwapChain( ImGuiViewport* pViewport, ImVec2 size )
    {
        using Internal                 = ImGuiRendererInternal;
        ImGuiRenderer* const pRenderer = Internal::findRenderer();
        if ( pRenderer == nullptr || pRenderer->_pDevice == nullptr || pViewport == nullptr || pViewport->RendererUserData == nullptr )
            return;
        pRenderer->_pDevice->resizeSwapChain( *static_cast<IRHISwapChain*>( pViewport->RendererUserData ), Internal::toPixels( size.x, pViewport->FramebufferScale.x ),
                                              Internal::toPixels( size.y, pViewport->FramebufferScale.y ) );
    }
} // namespace sw::editor
