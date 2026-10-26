#include "pch.h"

#include "Editor/Panels/RenderTarget/RenderTargetPanel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RenderTargetPanel, RenderTargetPanel::kPanelID, EditorPanelCategory::Tool, 900 );

    namespace
    {
        /// @brief 목록 열의 너비입니다. 이름이 길어도(GBufferAlbedo) 잘리지 않을 만큼 잡습니다.
        constexpr float32 kListColumns = 29.0f; ///< 목록 폭(글자 칸 수)
        /// @brief Auto 일 때 스냅샷을 다시 읽는 간격(UI 프레임)입니다. 읽기는 GPU 를 기다리므로 매 프레임 하지 않는다.
        constexpr uint32 kAutoRefreshFrames = 30;

        /** @brief 고른 타깃을 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const RenderTargetInfo* findByName( const vector<RenderTargetInfo>& listTarget, const string& name )
        {
            return VectorUtil::findBy<&RenderTargetInfo::_name>( listTarget, name );
        }
    } // namespace

    RenderTargetPanel::RenderTargetPanel()
        : IEditorPanel( false )
        , _lastGeneration{ 0 }
        , _pPreviewTextureID{ nullptr }
        , _previewTexture{ 0 }
        , _previewZoom{ 0.0f } // 0 = 창에 맞춤
        , _viewSettings{}
        , _snapshot{}
        , _snapshotTexture{ 0 }
        , _pSnapshotTextureID{ nullptr }
        , _snapshotDepthRange{ 0.0f, 1.0f }
        , _probedPixel{}
        , _probedTexel{}
        , _pendingReadbackSerial{ 0 }
        , _autoRefreshCountdown{ 0 }
        , _bRefreshRequested{ SW_FALSE }
        , _bSnapshotImageDirty{ SW_FALSE }
        , _bAutoRefresh{ SW_FALSE }
        , _bProbedPixel{ SW_FALSE }
    {
        // 선형화의 기본 면은 카메라 기본값이다. 다른 카메라를 보면 창에서 고친다.
        _viewSettings._nearPlane = CameraComponent::kDefaultNearZ;
        _viewSettings._farPlane  = CameraComponent::kDefaultFarZ;
    }

    void RenderTargetPanel::shutdown( IRHIDevice* pRHIDevice )
    {
        releasePreviewTexture();
        releaseSnapshotTexture( pRHIDevice );
    }

    bool RenderTargetPanel::hasSnapshot() const
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        return pSelected != nullptr && _snapshot._bSucceeded != SW_FALSE && _snapshot._texture == pSelected->_texture;
    }

    bool RenderTargetPanel::isSelectedDepth() const
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        return pSelected != nullptr && pSelected->_bDepth != SW_FALSE;
    }

    void RenderTargetPanel::releaseSnapshotTexture( IRHIDevice* pRHIDevice )
    {
        EditorContext*         pContext = editor::findSubsystem<EditorContext>();
        IImGuiRendererBackend* pBackend = pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRendererBackend() : nullptr;
        IRHIDevice*            pDevice  = pRHIDevice != nullptr ? pRHIDevice : ( pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr );
        if ( _pSnapshotTextureID != nullptr && pBackend != nullptr )
            pBackend->unregisterTexture( _pSnapshotTextureID );
        _pSnapshotTextureID = nullptr;

        const bool bCanReleaseSnapshot = _snapshotTexture != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr;
        if ( bCanReleaseSnapshot )
        {
            // 이미 낸 draw 스냅샷이 이 텍스처를 그릴 수 있다. ImGui 텍스처와 같은 큐에 맡겨 그 프레임들의 GPU 완료 뒤에 부순다.
            IRHIResourceFactory*   pResource = pDevice->getResourceFactory();
            const RHITextureHandle texture   = _snapshotTexture;
            if ( pBackend != nullptr )
            {
                pBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pResource, texture]()
                { pResource->destroyTexture( texture ); } ) );
            }
            else
            {
                // 이 분기는 에디터(게임) 스레드가 부른다. 렌더 스레드가 기록 중일 수 있어 직접 부수지 않고 기록 밖으로 미룬다(`IRHIDevice::releaseHandle`).
                pDevice->releaseHandle( RHIHandleKind::Texture, texture );
            }
        }
        _snapshotTexture = 0;
    }

    void RenderTargetPanel::releasePreviewTexture()
    {
        if ( _pPreviewTextureID == nullptr )
            return;

        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        // 컨텍스트가 이미 종료됐으면 놓아 줄 상대가 없다. 백엔드가 자기 힙을 통째로 버린 뒤다.
        if ( pContext != nullptr && pContext->getSubsystem<EditorRenderViews>().getRendererBackend() != nullptr )
            pContext->getSubsystem<EditorRenderViews>().getRendererBackend()->unregisterTexture( _pPreviewTextureID );
        _pPreviewTextureID = nullptr;
        _previewTexture    = 0;
    }

    void RenderTargetPanel::refreshTargetsIfStale()
    {
        // 엔진 서비스는 **에디터 로케이터**로 받는다. `EngineServices.h` 는 Engine/App/Test 전용이다.
        RenderTargetRegistry& registry = editor::getSubsystem<RenderTargetRegistry>();

        const uint64 generation = registry.getGeneration();
        if ( generation == _lastGeneration && _listTarget.empty() == false )
            return;

        _lastGeneration = generation;
        registry.snapshot( _listTarget );

        // 목록이 새로 만들어졌으면 보관하고 있던 텍스처 id 는 **이미 사라진 핸들**을 가리킬 수 있다.
        // 창 크기가 바뀌면 트랜지언트가 모두 다시 만들어지는데, 그때 그대로 그리면 백엔드가 죽는다.
        releasePreviewTexture();
    }

    void RenderTargetPanel::syncPreviewTexture()
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        // 깊이 첨부는 등록하지 않는다. 색으로 샘플링할 수 없어 CPU 스냅샷(선형화한 회색)으로 본다.
        const bool   bPreviewable = ( pSelected != nullptr ) && ( pSelected->_bDepth == SW_FALSE );
        const uint64 texture      = bPreviewable ? pSelected->_texture : 0;
        if ( texture == _previewTexture && _pPreviewTextureID != nullptr )
            return;

        releasePreviewTexture();
        if ( texture == 0 )
            return;

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( editorContext.getSubsystem<EditorRenderViews>().getRendererBackend() == nullptr )
            return;
        _pPreviewTextureID = editorContext.getSubsystem<EditorRenderViews>().getRendererBackend()->registerTexture( texture );
        _previewTexture    = ( _pPreviewTextureID != nullptr ) ? texture : 0;
    }

    void RenderTargetPanel::syncSnapshot()
    {
        RenderTargetRegistry&   registry           = editor::getSubsystem<RenderTargetRegistry>();
        const RenderTargetInfo* pSelected          = findByName( _listTarget, _selectedName );
        const bool              bTargetNotReadable = pSelected == nullptr || RenderTargetViewUtil::isDecodableFormat( pSelected->_format, pSelected->_bDepth != SW_FALSE ) == false;
        if ( bTargetNotReadable )
            return;

        // 도착한 결과를 받는다. 다른 타깃의 결과(고르기를 바꾼 사이 도착)는 버리고 다시 읽는다.
        if ( _pendingReadbackSerial != 0 && registry.getReadbackSerial() == _pendingReadbackSerial )
        {
            _pendingReadbackSerial = 0;
            if ( registry.copyReadback( _snapshot ) && _snapshot._texture == pSelected->_texture )
                _bSnapshotImageDirty = SW_TRUE;
        }

        // 다시 읽을 때: 고른 타깃의 스냅샷이 없을 때, Refresh, Auto 간격.
        bool bWantReadback = ( _snapshot._texture != pSelected->_texture ) || ( _bRefreshRequested != SW_FALSE );
        if ( _bAutoRefresh != SW_FALSE )
        {
            if ( _autoRefreshCountdown == 0 )
            {
                bWantReadback         = true;
                _autoRefreshCountdown = kAutoRefreshFrames;
            }
            else
            {
                --_autoRefreshCountdown;
            }
        }
        if ( bWantReadback && _pendingReadbackSerial == 0 )
        {
            _pendingReadbackSerial = registry.requestReadback( pSelected->_texture );
            _bRefreshRequested     = SW_FALSE;
        }

        if ( _bSnapshotImageDirty != SW_FALSE )
        {
            _bSnapshotImageDirty = SW_FALSE;
            rebuildSnapshotTexture();
        }
    }

    void RenderTargetPanel::rebuildSnapshotTexture()
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        releaseSnapshotTexture( nullptr );
        if ( pSelected == nullptr || hasSnapshot() == false )
            return;

        RenderTargetPreviewImage image{};
        if ( RenderTargetViewUtil::makePreviewImage( _snapshot._byte, _snapshot._layout, pSelected->_format, pSelected->_bDepth != SW_FALSE, _viewSettings,
                                                     image ) == false )
            return;
        _snapshotDepthRange = float2{ image._depthLow, image._depthHigh };

        EditorRenderViews& renderViews           = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
        IRHIDevice*        pDevice               = renderViews.getRHIDevice();
        const bool         bCannotUploadSnapshot = pDevice == nullptr || pDevice->getResourceFactory() == nullptr || renderViews.getRendererBackend() == nullptr;
        if ( bCannotUploadSnapshot )
            return;

        // 스냅샷마다 새 텍스처를 만든다. 쓰던 것에 다시 업로드하면 아직 그것을 그리는 프레임과 겹친다(옛 것은 해제 큐가 그 뒤에 부순다).
        RHITextureDesc desc{};
        desc._width                    = image._width;
        desc._height                   = image._height;
        desc._format                   = RHIFormat::R8G8B8A8_UNORM;
        desc._bIsShaderResource        = SW_TRUE;
        desc._mipLevels                = 1;
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        _snapshotTexture               = pResource->createTexture2D( desc );
        if ( _snapshotTexture == 0 )
            return;

        RHITextureUploadDesc upload{};
        upload._pData     = image._byte.data();
        upload._sizeBytes = static_cast<uint32>( image._byte.size() );
        upload._mipLevels = 1;
        if ( RHITransferUtil::uploadTexture2DAndWait( *pResource, _snapshotTexture, upload ) == false )
        {
            SW_LOG_WARNING( "Render target preview %#x%# could not be uploaded", image._width, image._height );
            releaseSnapshotTexture( pDevice );
            return;
        }
        _pSnapshotTextureID = renderViews.getRendererBackend()->registerTexture( _snapshotTexture );
    }

    void RenderTargetPanel::drawToolbar()
    {
        EditorWidgets::drawSearchField( "##rtsearch", _searchFilter, SW_LOCKEY( "EditorRenderTarget", "SearchHint" ), 260.0f );
        ImGui::SameLine();
        ImGui::TextDisabled( "%d targets", static_cast<int32>( _listTarget.size() ) );

        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 21.0f );
        // 0 = 창에 맞춤. 원본 해상도로 보고 싶을 때만 배율을 준다(1:1 픽셀 검사).
        ImGui::SliderFloat( "Zoom", &_previewZoom, 0.0f, 2.0f, _previewZoom <= 0.0f ? "fit" : "%.2fx" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "ZoomTip" ) );

        // 채널 · 깊이 · 픽셀 값은 CPU 로 읽은 스냅샷이다. 다시 읽기는 GPU 를 기다리므로 누를 때(또는 Auto 간격)만 한다.
        ImGui::SameLine();
        if ( ImGui::Button( "Refresh" ) )
            _bRefreshRequested = SW_TRUE;
        EditorSelfTestMarks::note( "renderTargets.refresh" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "RefreshTip" ) );
        ImGui::SameLine();
        bool bAuto = _bAutoRefresh != SW_FALSE;
        if ( ImGui::Checkbox( "Auto", &bAuto ) )
            _bAutoRefresh = bAuto ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "renderTargets.auto" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "AutoRefreshTip" ) );
    }

    void RenderTargetPanel::drawTargetList()
    {
        const EditorListFilter filter{ _searchFilter.c_str() };

        ImGui::BeginChild( "##rtlist", measureExtent( EditorTextExtent::ofText( kListColumns, 0.0f ) ), ImGuiChildFlags_Borders );
        uint32 shownCount = 0;
        for ( const RenderTargetInfo& info : _listTarget )
        {
            // 포맷 이름으로도 찾을 수 있어야 한다. "이 파이프라인의 HDR 타깃이 뭐지" 가 실제로 나오는 질문이다.
            const string_view formatName{ FrameRendererUtil::attachmentFormatName( info._format ) };
            if ( filter.matchesAny( { string_view{ info._name }, formatName } ) == false )
                continue;

            ++shownCount;
            const bool bSelected = ( info._name == _selectedName );
            if ( ImGui::Selectable( info._name.c_str(), bSelected ) )
                _selectedName = info._name;
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( "renderTargets.row." + info._name ).c_str() );

            if ( info._bDepth != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(depth)" );
            }
            else if ( info._bPresented != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(screen)" );
            }
        }

        if ( _listTarget.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "NoTargets" ) );
        else if ( shownCount == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "NoFilterMatch" ) );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawViewControls( const RenderTargetInfo& selected )
    {
        if ( selected._bDepth != SW_FALSE )
        {
            // 깊이는 한 채널이다. 원근 깊이는 먼 쪽에 몰려 있어 그대로 보면 거의 흰색이라, 거리로 되돌리고 화면의 범위로 늘린다.
            bool bChanged   = false;
            bool bLinearize = _viewSettings._bLinearizeDepth != SW_FALSE;
            bool bAutoRange = _viewSettings._bAutoDepthRange != SW_FALSE;
            if ( ImGui::Checkbox( "Linearize", &bLinearize ) )
            {
                _viewSettings._bLinearizeDepth = bLinearize ? SW_TRUE : SW_FALSE;
                bChanged                       = true;
            }
            EditorSelfTestMarks::note( "renderTargets.linearize" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "LinearizeTip" ) );
            ImGui::SameLine();
            if ( ImGui::Checkbox( "Auto range", &bAutoRange ) )
            {
                _viewSettings._bAutoDepthRange = bAutoRange ? SW_TRUE : SW_FALSE;
                bChanged                       = true;
            }
            EditorSelfTestMarks::note( "renderTargets.autoRange" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "AutoRangeTip" ) );
            if ( bLinearize )
            {
                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 24.0f );
                float32 arrPlane[2]{ _viewSettings._nearPlane, _viewSettings._farPlane };
                if ( ImGui::DragFloat2( "Near/Far", arrPlane, 0.1f, 0.001f, 100000.0f, "%.3f" ) )
                {
                    _viewSettings._nearPlane = MathUtil::max( arrPlane[0], 0.001f );
                    _viewSettings._farPlane  = MathUtil::max( arrPlane[1], _viewSettings._nearPlane + 0.001f );
                    bChanged                 = true;
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "NearFarTip" ) );
            }
            if ( hasSnapshot() )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "range %.3f .. %.3f", static_cast<float64>( _snapshotDepthRange._x ), static_cast<float64>( _snapshotDepthRange._y ) );
            }
            if ( bChanged )
                _bSnapshotImageDirty = SW_TRUE;
            return;
        }

        for ( uint32 channelIndex = 0; channelIndex < static_cast<uint32>( RenderTargetChannel::Count ); ++channelIndex )
        {
            const RenderTargetChannel channel = static_cast<RenderTargetChannel>( channelIndex );
            if ( channelIndex > 0 )
                ImGui::SameLine();
            if ( ImGui::RadioButton( RenderTargetViewUtil::getChannelLabel( channel ), _viewSettings._channel == channel ) && _viewSettings._channel != channel )
            {
                _viewSettings._channel = channel;
                _bSnapshotImageDirty   = SW_TRUE;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "ChannelTip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( string{ "renderTargets.channel." } + RenderTargetViewUtil::getChannelLabel( channel ) ).c_str() );
        }
    }

    void RenderTargetPanel::probeHoveredPixel( const RenderTargetInfo& selected, const float2& imageMin, const float2& imageSize )
    {
        if ( ImGui::IsItemHovered() == false || hasSnapshot() == false )
            return;
        const ImVec2 mouse = ImGui::GetMousePos();
        int2         texel{};
        if ( RenderTargetViewUtil::mapPreviewToTexel( float2{ mouse.x - imageMin._x, mouse.y - imageMin._y }, imageSize, _snapshot._layout._width,
                                                      _snapshot._layout._height, texel ) == false )
            return;
        float4     value{};
        const bool bDepth = selected._bDepth != SW_FALSE;
        if ( RenderTargetViewUtil::readPixel( _snapshot._byte, _snapshot._layout, selected._format, bDepth, static_cast<uint32>( texel._x ),
                                              static_cast<uint32>( texel._y ), value ) == false )
            return;
        _probedPixel  = value;
        _probedTexel  = texel;
        _bProbedPixel = SW_TRUE;

        ImGui::BeginTooltip();
        ImGui::Text( "(%d, %d)", texel._x, texel._y );
        if ( bDepth )
        {
            ImGui::Text( "%s %.6f", SW_LOCKEY( "EditorRenderTarget", "DepthLabel" ), static_cast<float64>( value._x ) );
            ImGui::Text( "%s %.3f", SW_LOCKEY( "EditorRenderTarget", "LinearLabel" ),
                         static_cast<float64>( RenderTargetViewUtil::linearizeDepth( value._x, _viewSettings._nearPlane, _viewSettings._farPlane ) ) );
        }
        else
        {
            const uint32  channelCount = RenderTargetViewUtil::getChannelCount( selected._format, false );
            const float32 arrValue[4]{ value._x, value._y, value._z, value._w };
            const utf8*   arrLabel[4]{ "R", "G", "B", "A" };
            for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
            {
                ImGui::Text( "%s %.4f", arrLabel[channelIndex], static_cast<float64>( arrValue[channelIndex] ) );
            }
        }
        ImGui::EndTooltip();
    }

    void RenderTargetPanel::drawPreview()
    {
        ImGui::BeginChild( "##rtpreview", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );

        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        if ( pSelected == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "PickTargetHint" ) );
            ImGui::EndChild();
            return;
        }

        ImGui::TextUnformatted( pSelected->_name.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%ux%u  %s", pSelected->_width, pSelected->_height,
                             FrameRendererUtil::attachmentFormatName( pSelected->_format ) );

        const bool bDepth     = pSelected->_bDepth != SW_FALSE;
        const bool bDecodable = RenderTargetViewUtil::isDecodableFormat( pSelected->_format, bDepth );
        if ( bDecodable )
            drawViewControls( *pSelected );

        // 마지막으로 읽은 픽셀 값 줄. 미리보기 밖으로 나가도 남아 값을 옮겨 적을 수 있다.
        if ( _bProbedPixel != SW_FALSE )
        {
            if ( bDepth )
            {
                ImGui::TextDisabled( "Pixel (%d, %d): depth %.6f", _probedTexel._x, _probedTexel._y, static_cast<float64>( _probedPixel._x ) );
            }
            else
            {
                ImGui::TextDisabled( "Pixel (%d, %d): %.4f %.4f %.4f %.4f", _probedTexel._x, _probedTexel._y, static_cast<float64>( _probedPixel._x ),
                                     static_cast<float64>( _probedPixel._y ), static_cast<float64>( _probedPixel._z ), static_cast<float64>( _probedPixel._w ) );
            }
        }

        // 색 그대로(RGBA)는 GPU 텍스처를 그대로 보인다(매 프레임 최신). 깊이 · 채널 하나는 CPU 스냅샷으로 만든 이미지다.
        const bool bLive      = ( bDepth == false ) && ( _viewSettings._channel == RenderTargetChannel::RGBA );
        void*      pTextureID = bLive ? _pPreviewTextureID : _pSnapshotTextureID;
        if ( pTextureID == nullptr )
        {
            if ( bLive )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "RegisterFailed" ) );
            else if ( bDecodable == false )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "FormatNotReadable" ) );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "ReadingTarget" ) );
            ImGui::EndChild();
            return;
        }

        const ImVec2  avail  = ImGui::GetContentRegionAvail();
        const float32 aspect = ( pSelected->_height > 0 )
                                 ? ( static_cast<float32>( pSelected->_width ) / static_cast<float32>( pSelected->_height ) )
                                 : ( 16.0f / 9.0f );

        float32 drawWidth  = 0.0f;
        float32 drawHeight = 0.0f;
        if ( _previewZoom <= 0.0f )
        {
            // 창에 맞춘다. 가로 · 세로 중 먼저 걸리는 쪽에 맞춰야 비율이 유지된다.
            drawWidth  = MathUtil::max( avail.x, 1.0f );
            drawHeight = drawWidth / MathUtil::max( aspect, 0.001f );
            if ( drawHeight > avail.y && avail.y > 1.0f )
            {
                drawHeight = avail.y;
                drawWidth  = drawHeight * aspect;
            }
        }
        else
        {
            drawWidth  = static_cast<float32>( pSelected->_width ) * _previewZoom;
            drawHeight = static_cast<float32>( pSelected->_height ) * _previewZoom;
        }

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ drawWidth, drawHeight } );
        EditorSelfTestMarks::note( "renderTargets.preview" );
        probeHoveredPixel( *pSelected, float2{ imageMin.x, imageMin.y }, float2{ drawWidth, drawHeight } );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawContent()
    {
        refreshTargetsIfStale();

        // 고른 것이 사라졌으면(파이프라인이 바뀌었다) 선택을 놓는다. 없는 이름을 보관하고 있으면
        // 미리보기가 영영 빈 채로 남는다.
        if ( _selectedName.empty() == false && findByName( _listTarget, _selectedName ) == nullptr )
            _selectedName.clear();

        // 고른 타깃이 바뀌면 읽어 둔 픽셀 값은 다른 이미지의 것이다.
        if ( _snapshot._texture != 0 && hasSnapshot() == false )
            _bProbedPixel = SW_FALSE;

        // 아무것도 고르지 않았으면 **화면에 나가는 것**을 고른다. 열자마자 지금 보이는 이미지가 뜬다.
        // 이름순 첫 번째로 두면 AOColor 가 잡혀서 "이게 뭐지" 부터 시작하게 된다.
        if ( _selectedName.empty() )
        {
            for ( const RenderTargetInfo& info : _listTarget )
            {
                if ( info._bPresented != SW_FALSE )
                {
                    _selectedName = info._name;
                    break;
                }
            }
        }
        if ( _selectedName.empty() )
        {
            for ( const RenderTargetInfo& info : _listTarget )
            {
                if ( info._bDepth == SW_FALSE )
                {
                    _selectedName = info._name;
                    break;
                }
            }
        }

        syncPreviewTexture();
        syncSnapshot();

        drawToolbar();
        ImGui::Separator();
        drawTargetList();
        ImGui::SameLine();
        drawPreview();
    }
} // namespace sw::editor
