#include "pch.h"

#include "Editor/Panels/RenderTarget/Model/RenderTargetViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw::editor
{
    namespace
    {
        /// @brief 자동일 때 스냅샷을 다시 읽는 간격(UI 프레임)입니다. 읽기는 GPU 를 기다리므로 매 프레임 하지 않는다.
        constexpr uint32 kAutoRefreshFrames = 30;

        /** @brief 타깃을 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const RenderTargetInfo* findByName( const vector<RenderTargetInfo>& listTarget, const string& name )
        {
            return VectorUtil::findBy<&RenderTargetInfo::_name>( listTarget, name );
        }

        /** @brief 지금 ImGui 렌더러입니다(에디터 컨텍스트가 먼저 내려갔으면 nullptr). */
        ImGuiRenderer* findRenderer()
        {
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            return pContext != nullptr ? pContext->getSubsystem<EditorRenderViews>().getRenderer() : nullptr;
        }
    } // namespace

    RenderTargetViewModel::RenderTargetViewModel()
        : _listTarget{}
        , _listSerial{ 0 }
        , _selectedName{}
        , _lastGeneration{ 0 }
        , _pPreviewTextureID{ nullptr }
        , _previewTexture{ 0 }
        , _viewSettings{}
        , _snapshot{}
        , _snapshotPath{}
        , _snapshotHandle{}
        , _snapshotSerial{ 0 }
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

    void RenderTargetViewModel::gather( RenderTargetViewState& outState )
    {
        refreshTargetsIfStale();
        commitSelection();
        // 고른 타깃이 바뀌면 읽어 둔 픽셀 값은 다른 이미지의 것이다.
        if ( _snapshot._texture != 0 && hasSnapshot() == false )
            _bProbedPixel = SW_FALSE;
        syncPreviewTexture();
        syncSnapshot();
        applySnapshotTextureIfReady();

        if ( outState._listSerial != _listSerial )
        {
            outState._listTarget = _listTarget;
            outState._listSerial = _listSerial;
        }
        outState._selectedName       = _selectedName;
        outState._viewSettings       = _viewSettings;
        outState._pLiveTextureID     = _pPreviewTextureID;
        outState._pSnapshotTextureID = _pSnapshotTextureID;
        outState._snapshotDepthRange = _snapshotDepthRange;
        outState._probedPixel        = _probedPixel;
        outState._probedTexel        = _probedTexel;
        outState._bHasSnapshot       = hasSnapshot() ? SW_TRUE : SW_FALSE;
        outState._bAutoRefresh       = _bAutoRefresh;
        outState._bProbedPixel       = _bProbedPixel;
    }

    void RenderTargetViewModel::shutdown()
    {
        releasePreviewTexture();
        releaseSnapshotTexture();
    }

    void RenderTargetViewModel::selectTarget( const string& name )
    {
        _selectedName = name;
    }

    void RenderTargetViewModel::requestRefresh()
    {
        _bRefreshRequested = SW_TRUE;
    }

    void RenderTargetViewModel::setAutoRefresh( bool bAutoRefresh )
    {
        _bAutoRefresh = bAutoRefresh ? SW_TRUE : SW_FALSE;
    }

    void RenderTargetViewModel::setChannel( RenderTargetChannel channel )
    {
        if ( _viewSettings._channel == channel )
            return;
        _viewSettings._channel = channel;
        _bSnapshotImageDirty   = SW_TRUE;
    }

    void RenderTargetViewModel::setLinearizeDepth( bool bLinearize )
    {
        _viewSettings._bLinearizeDepth = bLinearize ? SW_TRUE : SW_FALSE;
        _bSnapshotImageDirty           = SW_TRUE;
    }

    void RenderTargetViewModel::setAutoDepthRange( bool bAutoRange )
    {
        _viewSettings._bAutoDepthRange = bAutoRange ? SW_TRUE : SW_FALSE;
        _bSnapshotImageDirty           = SW_TRUE;
    }

    void RenderTargetViewModel::setDepthPlanes( float32 nearPlane, float32 farPlane )
    {
        _viewSettings.setDepthPlanes( nearPlane, farPlane );
        _bSnapshotImageDirty = SW_TRUE;
    }

    bool RenderTargetViewModel::probePixel( const float2& localPos, const float2& drawSize, RenderTargetPixelProbe& outProbe )
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        if ( pSelected == nullptr || hasSnapshot() == false )
            return false;
        int2 texel{};
        if ( RenderTargetViewSettings::mapPreviewToTexel( localPos, drawSize, _snapshot._layout._width, _snapshot._layout._height, texel ) == false )
            return false;
        float4     value{};
        const bool bDepth = pSelected->_bDepth != SW_FALSE;
        if ( RenderTargetReadback::readPixel( _snapshot._byte, _snapshot._layout, pSelected->_format, bDepth, static_cast<uint32>( texel._x ),
                                              static_cast<uint32>( texel._y ), value ) == false )
            return false;
        _probedPixel  = value;
        _probedTexel  = texel;
        _bProbedPixel = SW_TRUE;

        outProbe._texel    = texel;
        outProbe._value    = value;
        outProbe._bDepth   = bDepth ? SW_TRUE : SW_FALSE;
        outProbe._bGBuffer = SW_FALSE;
        outProbe._listDecodedValue.clear();
        if ( bDepth )
        {
            outProbe._linearDepth  = RenderTargetViewSettings::linearizeDepth( value._x, _viewSettings._nearPlane, _viewSettings._farPlane );
            outProbe._channelCount = 1;
            return true;
        }
        // G버퍼 첨부면 명세(`GBufferAttachmentSpec`)의 뜻으로 채널을 이름 짓고 값을 푼다(노멀은 옥타헤드럴을 풀어 단위 벡터) — 셰이더 읽기와 같은 식이다.
        const GBufferAttachmentSpec* pSpec = GBufferLayout::findAttachment( pSelected->_name );
        outProbe._channelCount             = RenderTargetReadback::getChannelCount( pSelected->_format, false );
        for ( uint32 channelIndex = 0; channelIndex < 4; ++channelIndex )
            outProbe._arrMeaning[channelIndex] = ( pSpec != nullptr && channelIndex < pSpec->_channelCount ) ? pSpec->_arrChannel[channelIndex]._pMeaning : nullptr;
        if ( pSpec != nullptr )
        {
            outProbe._bGBuffer = SW_TRUE;
            GBufferLayout::decodeTexel( *pSpec, value, outProbe._listDecodedValue );
        }
        return true;
    }

    void RenderTargetViewModel::refreshTargetsIfStale()
    {
        // 엔진 서비스는 **에디터 로케이터**로 받는다. `EngineServices.h` 는 Engine/App/Test 전용이다.
        RenderTargetRegistry& registry = editor::getSubsystem<RenderTargetRegistry>();

        const uint64 generation = registry.getGeneration();
        if ( generation == _lastGeneration && _listTarget.empty() == false )
            return;

        _lastGeneration = generation;
        registry.snapshot( _listTarget );
        ++_listSerial;

        // 목록이 새로 만들어졌으면 보관하고 있던 텍스처 id 는 **이미 사라진 핸들**을 가리킬 수 있다.
        // 창 크기가 바뀌면 트랜지언트가 모두 다시 만들어지는데, 그때 그대로 그리면 백엔드가 죽는다.
        releasePreviewTexture();
    }

    void RenderTargetViewModel::commitSelection()
    {
        // 고른 것이 사라졌으면(파이프라인이 바뀌었다) 선택을 놓는다. 없는 이름을 보관하고 있으면 미리보기가 영영 빈 채로 남는다.
        if ( _selectedName.empty() == false && findByName( _listTarget, _selectedName ) == nullptr )
            _selectedName.clear();
        if ( _selectedName.empty() == false )
            return;

        // 아무것도 고르지 않았으면 **화면에 나가는 것**을 고른다. 열자마자 지금 보이는 이미지가 뜬다.
        // 이름순 첫 번째로 두면 AOColor 가 잡혀서 "이게 뭐지" 부터 시작하게 된다.
        for ( const RenderTargetInfo& info : _listTarget )
        {
            if ( info._bPresented != SW_FALSE )
            {
                _selectedName = info._name;
                return;
            }
        }
        for ( const RenderTargetInfo& info : _listTarget )
        {
            if ( info._bDepth == SW_FALSE )
            {
                _selectedName = info._name;
                return;
            }
        }
    }

    bool RenderTargetViewModel::hasSnapshot() const
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        return pSelected != nullptr && _snapshot._bSucceeded != SW_FALSE && _snapshot._texture == pSelected->_texture;
    }

    void RenderTargetViewModel::releaseSnapshotTexture()
    {
        ImGuiRenderer* pBackend = findRenderer();
        if ( _pSnapshotTextureID != nullptr && pBackend != nullptr )
            pBackend->unregisterTexture( _pSnapshotTextureID );
        _pSnapshotTextureID = nullptr;
        if ( _snapshotPath.empty() )
            return;
        // 이미 낸 draw 스냅샷이 이 텍스처를 그릴 수 있다 — 참조(로드 핸들)는 그리기 해제 큐에 맡겨 그 프레임들 뒤에 놓고, 마지막 참조가 사라지면 엔진이 GPU 자원을 놓는다.
        if ( _snapshotHandle.isValid() && pBackend != nullptr )
        {
            auto pHold = sw::make_shared<TextureHandle>( std::move( _snapshotHandle ) );
            pBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pHold]()
            { *pHold = TextureHandle{}; } ) );
        }
        _snapshotHandle = TextureHandle{};
        if ( AssetManager* pAssets = editor::findSubsystem<AssetManager>() )
            pAssets->getAssetCache<TextureCache>().releaseCPUTexture( _snapshotPath );
        _snapshotPath.clear();
    }

    void RenderTargetViewModel::applySnapshotTextureIfReady()
    {
        if ( _pSnapshotTextureID != nullptr || _snapshotHandle.isReady() == false )
            return;
        ImGuiRenderer*   pBackend = findRenderer();
        const Texture2D* pTexture = editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>().find( _snapshotPath );
        if ( pTexture == nullptr || pTexture->isRHIValid() == false || pBackend == nullptr )
            return;
        _pSnapshotTextureID = pBackend->registerTexture( pTexture->getHandle() );
    }

    void RenderTargetViewModel::releasePreviewTexture()
    {
        if ( _pPreviewTextureID == nullptr )
            return;
        // 컨텍스트가 이미 종료됐으면 놓아 줄 상대가 없다. 백엔드가 자기 힙을 통째로 버린 뒤다.
        if ( ImGuiRenderer* pBackend = findRenderer() )
            pBackend->unregisterTexture( _pPreviewTextureID );
        _pPreviewTextureID = nullptr;
        _previewTexture    = 0;
    }

    void RenderTargetViewModel::syncPreviewTexture()
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

        ImGuiRenderer* pBackend = findRenderer();
        if ( pBackend == nullptr )
            return;
        _pPreviewTextureID = pBackend->registerTexture( texture );
        _previewTexture    = ( _pPreviewTextureID != nullptr ) ? texture : 0;
    }

    void RenderTargetViewModel::syncSnapshot()
    {
        RenderTargetRegistry&   registry           = editor::getSubsystem<RenderTargetRegistry>();
        const RenderTargetInfo* pSelected          = findByName( _listTarget, _selectedName );
        const bool              bTargetNotReadable = pSelected == nullptr || RenderTargetReadback::isDecodableFormat( pSelected->_format, pSelected->_bDepth != SW_FALSE ) == false;
        if ( bTargetNotReadable )
            return;

        // 도착한 결과를 받는다. 다른 타깃의 결과(고르기를 바꾼 사이 도착)는 버리고 다시 읽는다.
        if ( _pendingReadbackSerial != 0 && registry.getReadbackSerial() == _pendingReadbackSerial )
        {
            _pendingReadbackSerial = 0;
            if ( registry.copyReadback( _snapshot ) && _snapshot._texture == pSelected->_texture )
                _bSnapshotImageDirty = SW_TRUE;
        }

        // 다시 읽을 때: 고른 타깃의 스냅샷이 없을 때, 다시 읽기 요청, 자동 간격.
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

    void RenderTargetViewModel::rebuildSnapshotTexture()
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        releaseSnapshotTexture();
        if ( pSelected == nullptr || hasSnapshot() == false )
            return;

        RenderTargetPreviewImage image{};
        if ( RenderTargetViewSettings::makePreviewImage( _snapshot._byte, _snapshot._layout, pSelected->_format, pSelected->_bDepth != SW_FALSE, _viewSettings,
                                                         image ) == false )
            return;
        _snapshotDepthRange = float2{ image._depthLow, image._depthHigh };

        IRHIDevice* const pDevice = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().getRHIDevice();
        if ( pDevice == nullptr )
            return;
        // 스냅샷마다 새 경로다. 쓰던 것에 다시 올리면 아직 그것을 그리는 프레임과 겹친다(옛 것은 위 해제가 그 프레임들 뒤에 놓는다).
        TextureCache&                            textures = editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        StringBuilder<constant::buffer::kMax256> snapshotPathBuilder;
        snapshotPathBuilder.appendFormat( "%#editor/rendertargetpreview%#", TextureCache::kCPUTexturePrefix, ++_snapshotSerial );
        _snapshotPath = string( snapshotPathBuilder.view() );
        if ( textures.createCPUTexture( _snapshotPath, image._width, image._height, std::move( image._byte ) ) == false )
        {
            SW_LOG_WARNING_ID( "RenderTargetPanel.RenderTargetPreviewX", "Render target preview %#x%# could not be made ('%#')", image._width, image._height, _snapshotPath.c_str() );
            _snapshotPath.clear();
            return;
        }
        _snapshotHandle = textures.request( _snapshotPath, AssetLoadStart::Async );
        applySnapshotTextureIfReady();
    }
} // namespace sw::editor
