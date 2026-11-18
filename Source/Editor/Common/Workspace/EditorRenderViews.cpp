#include "pch.h"

#include "Editor/Common/Workspace/EditorRenderViews.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw::editor
{
    void EditorRenderViews::publishUIFrame( uint32 uiRedrawReasons )
    {
        _uiFrame._redrawReasons = uiRedrawReasons;
        ScopedLock<mutex> lock{ _publishedMutex };
        _publishedFrame = _uiFrame;
    }

    EditorUIFrameOutput EditorRenderViews::getPublishedUIFrame() const
    {
        ScopedLock<mutex> lock{ _publishedMutex };
        return _publishedFrame;
    }

    void EditorRenderViews::shutdownSubsystem()
    {
        destroyViewTargets();
        _pRenderer  = nullptr;
        _pRHIDevice = nullptr;
    }

    void EditorRenderViews::clearViewDrawnMarks()
    {
        // UI 프레임 시작 — 그리기 표시를 지우고, 게임 스레드가 넘긴 타깃 값(받은 렌더 텍스처 · 요청한 크기)을 이번 프레임 값으로 받는다.
        {
            ScopedLock<mutex> lock{ _targetStatesMutex };
            _uiTargets = _publishedTargets;
        }
        for ( EditorUIFrameOutput::ViewMark& mark : _uiFrame._arrView )
        {
            mark._bDrawn = SW_FALSE;
        }
        _uiFrame._previewCount        = 0;
        _uiFrame._bAnimationRequested = SW_FALSE;
    }

    void EditorRenderViews::markViewDrawn( EditorViewKind kind, const float2& imageClientPos )
    {
        EditorUIFrameOutput::ViewMark& mark = _uiFrame._arrView[static_cast<uint32>( kind )];
        mark._windowX                       = static_cast<int32>( MathUtil::round( imageClientPos._x ) );
        mark._windowY                       = static_cast<int32>( MathUtil::round( imageClientPos._y ) );
        mark._bDrawn                        = SW_TRUE;
    }

    void EditorRenderViews::destroyViewTargets()
    {
        for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( EditorViewKind::Count ); ++kindIndex )
        {
            destroyViewTarget( static_cast<EditorViewKind>( kindIndex ) );
        }
        for ( EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            destroyTarget( preview._target );
        }
        _listPreviewTarget.clear();
    }

    void EditorRenderViews::destroyViewTarget( EditorViewKind kind )
    {
        destroyTarget( _arrViewTarget[static_cast<uint32>( kind )] );
    }

    void EditorRenderViews::ensureViewTargetSize( EditorViewKind kind, uint32 width, uint32 height )
    {
        ensureTargetSize( _arrViewTarget[static_cast<uint32>( kind )], width, height );
    }

    const EditorPreviewTargetState* EditorRenderViews::findUIPreviewTargetState( uint32 id ) const
    {
        return VectorUtil::findBy<&EditorPreviewTargetState::_id>( _uiTargets._listPreview, id );
    }

    void EditorRenderViews::applyReadyAndPublishTargets()
    {
        EditorTargetSnapshot states;
        for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( EditorViewKind::Count ); ++kindIndex )
        {
            applyIfReady( _arrViewTarget[kindIndex] );
            states._arrView[kindIndex] = EditorViewTargetState::capture( _arrViewTarget[kindIndex] );
        }
        states._listPreview.reserve( _listPreviewTarget.size() );
        for ( EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            applyIfReady( preview._target );
            states._listPreview.push_back( EditorPreviewTargetState{ EditorViewTargetState::capture( preview._target ), preview._id } );
        }
        ScopedLock<mutex> lock{ _targetStatesMutex };
        _publishedTargets = std::move( states );
    }

    void EditorRenderViews::freePreviewTarget( uint32 id )
    {
        for ( size_t index = 0; index < _listPreviewTarget.size(); ++index )
        {
            if ( _listPreviewTarget[index]._id != id )
                continue;
            destroyTarget( _listPreviewTarget[index]._target );
            _listPreviewTarget.erase( _listPreviewTarget.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    void EditorRenderViews::ensurePreviewTargetSize( uint32 id, uint32 width, uint32 height )
    {
        if ( id == 0 )
            return;
        EditorPreviewViewTarget* pPreview = findPreviewTargetMutable( id );
        if ( pPreview == nullptr )
        {
            EditorPreviewViewTarget created;
            created._id = id;
            _listPreviewTarget.push_back( created );
            pPreview = &_listPreviewTarget.back();
        }
        ensureTargetSize( pPreview->_target, width, height );
    }

    const EditorPreviewViewTarget* EditorRenderViews::findPreviewTarget( uint32 id ) const
    {
        return VectorUtil::findBy<&EditorPreviewViewTarget::_id>( _listPreviewTarget, id );
    }

    EditorPreviewViewTarget* EditorRenderViews::findPreviewTargetMutable( uint32 id )
    {
        return VectorUtil::findBy<&EditorPreviewViewTarget::_id>( _listPreviewTarget, id );
    }

    void EditorRenderViews::submitPreviewView( uint32 id, uint32 sceneKey, const float4x4& viewProj, const float3& cameraPos )
    {
        if ( id == 0 )
            return;
        requestAnimationFrame(); // 보인 미리보기 씬은 이번 프레임에 틱한다(`ScenePreviewTick::WhenDrawn`) — 그 움직임을 그리려면 다음 프레임이 곧 와야 한다
        if ( _uiFrame._previewCount >= kMaxPreviewSceneView )
        {
            if ( _bPreviewOverflowLogged == SW_FALSE )
                SW_LOG_ERROR_ID( "EditorRenderViews.PreviewViewOverflow", "More than %# preview views were shown in one frame - the rest are not drawn", kMaxPreviewSceneView );
            _bPreviewOverflowLogged = SW_TRUE;
            return;
        }
        EditorUIFrameOutput::PreviewRequest& submit = _uiFrame._arrPreview[_uiFrame._previewCount++];
        submit._id                                  = id;
        submit._sceneKey                            = sceneKey;
        submit._viewProj                            = viewProj;
        submit._cameraPos                           = cameraPos;
    }

    uint32 EditorRenderViews::collectSceneViews( const EditorUIFrameOutput& frame, PreviewSceneView* pOutView, uint32 capacity ) const
    {
        uint32 count = 0;
        for ( uint32 submitIndex = 0; submitIndex < frame._previewCount && count < capacity; ++submitIndex )
        {
            const EditorUIFrameOutput::PreviewRequest& submit   = frame._arrPreview[submitIndex];
            const EditorPreviewViewTarget* const       pPreview = findPreviewTarget( submit._id ); // 넘긴 뒤 창이 닫혔으면 없다
            const bool                                 bVisible = pPreview != nullptr && pPreview->_target._renderTarget != 0 && submit._sceneKey != 0;
            if ( bVisible == false )
                continue;
            PreviewSceneView& view     = pOutView[count];
            view                       = PreviewSceneView{};
            view._sceneKey             = submit._sceneKey;
            view._viewProj             = submit._viewProj;
            view._cameraPos            = submit._cameraPos;
            view._target._renderTarget = pPreview->_target._renderTarget;
            view._target._width        = pPreview->_target._width;
            view._target._height       = pPreview->_target._height;
            ++count;
        }
        return count;
    }

    void EditorRenderViews::releaseCurrent( EditorViewTarget& view )
    {
        if ( view._pTextureID != nullptr && _pRenderer != nullptr )
        {
            _pRenderer->unregisterTexture( view._pTextureID );
            view._pTextureID = nullptr;
        }
        // 줄 서 있는 패킷이 이 렌더 타깃에 그리고, 이미 낸 draw 스냅샷이 그것을 샘플링한다 — 참조(로드 핸들)는 그리기 해제 큐에 맡겨 그 프레임들 뒤에 놓는다.
        // 마지막 참조가 사라지면 엔진이 GPU 자원을 놓는다. 그릴 쪽(렌더러 백엔드)이 없으면 지금 놓는다.
        if ( view._texture.isValid() && _pRenderer != nullptr )
        {
            auto pHold = sw::make_shared<TextureHandle>( std::move( view._texture ) );
            _pRenderer->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pHold]()
            { *pHold = TextureHandle{}; } ) );
        }
        view._texture      = TextureHandle{};
        view._texturePath  = string{};
        view._renderTarget = 0;
        view._width        = 0;
        view._height       = 0;
    }

    void EditorRenderViews::destroyTarget( EditorViewTarget& view )
    {
        releaseCurrent( view );
        view._pendingTexture = TextureHandle{}; // 아무도 그리지 않았다 — 바로 놓는다
        view._pendingPath    = string{};
        view._pendingWidth   = 0;
        view._pendingHeight  = 0;
    }

    void EditorRenderViews::ensureTargetSize( EditorViewTarget& view, uint32 width, uint32 height )
    {
        if ( width == 0 || height == 0 || _pRHIDevice == nullptr )
            return;
        const bool bAlreadyRequested = width == view.getRequestedWidth() && height == view.getRequestedHeight() && ( view._texture.isValid() || view._pendingTexture.isValid() );
        if ( bAlreadyRequested == false )
        {
            // 새 크기는 바꾸는 중인 칸에 요청한다 — 지금 타깃은 새 것이 준비될 때까지 그대로 그린다.
            TextureCache&                            textures = editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            StringBuilder<constant::buffer::kMax256> pendingPathBuilder;
            pendingPathBuilder.appendFormat( "%#editor/view%#", TextureCache::kRenderTargetPrefix, _nextTargetSerial++ );
            view._pendingPath = string( pendingPathBuilder.view() );
            textures.declareRenderTarget( view._pendingPath, width, height );
            view._pendingTexture = textures.request( view._pendingPath, AssetLoadStart::Async, TextureStreamingGroup::NeverStream );
            view._pendingWidth   = width;
            view._pendingHeight  = height;
            if ( view._pendingTexture.isValid() == false )
                SW_LOG_ERROR_ID( "EditorRenderViews.RequestFailed", "View target %#x%# could not be requested ('%#')", width, height, view._pendingPath.c_str() );
        }
        applyIfReady( view );
    }

    void EditorRenderViews::applyIfReady( EditorViewTarget& view )
    {
        if ( view._pendingTexture.isReady() == false )
            return; // 바꿀 것이 없거나, 엔진이 아직 올리지 않았다(다음 프레임에 다시 본다)
        const Texture2D* pTexture = editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>().find( view._pendingPath );
        if ( pTexture == nullptr || pTexture->isRHIValid() == false )
            return;
        releaseCurrent( view );
        view._texture        = std::move( view._pendingTexture );
        view._texturePath    = std::move( view._pendingPath );
        view._width          = view._pendingWidth;
        view._height         = view._pendingHeight;
        view._pendingTexture = TextureHandle{};
        view._pendingPath    = string{};
        view._renderTarget   = pTexture->getHandle();
        if ( _pRenderer != nullptr )
            view._pTextureID = _pRenderer->registerTexture( view._renderTarget );
    }
} // namespace sw::editor
