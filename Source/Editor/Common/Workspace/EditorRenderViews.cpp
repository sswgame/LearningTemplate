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
    bool EditorRenderViews::isViewRequested( EditorViewKind kind ) const
    {
        const bool bSceneDrawn = wasViewDrawn( EditorViewKind::Scene );
        const bool bGameDrawn  = wasViewDrawn( EditorViewKind::Game );
        return kind == EditorViewKind::Scene ? EditorViewTargetUtil::shouldRequestSceneView( bSceneDrawn, bGameDrawn ) : EditorViewTargetUtil::shouldRequestGameView( bGameDrawn );
    }

    void EditorRenderViews::shutdownSubsystem()
    {
        destroyViewTargets();
        _pRenderer  = nullptr;
        _pRHIDevice = nullptr;
    }

    void EditorRenderViews::clearViewDrawnMarks()
    {
        // UI 프레임 시작 — 그리기 표시를 지우고, 엔진이 그사이 올린 렌더 텍스처를 받는다(패널은 크기가 바뀔 때만 `ensure*TargetSize` 를 부른다).
        for ( EditorViewTarget& view : _arrViewTarget )
        {
            view._bDrawn = SW_FALSE;
            applyIfReady( view );
        }
        for ( EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            preview._target._bDrawn = SW_FALSE;
            applyIfReady( preview._target );
        }
        _bAnimationRequested = SW_FALSE;
    }

    void EditorRenderViews::markViewDrawn( EditorViewKind kind, const float2& imageClientPos )
    {
        EditorViewTarget& view = _arrViewTarget[static_cast<uint32>( kind )];
        view._windowX          = static_cast<int32>( MathUtil::round( imageClientPos._x ) );
        view._windowY          = static_cast<int32>( MathUtil::round( imageClientPos._y ) );
        view._bDrawn           = SW_TRUE;
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

    uint32 EditorRenderViews::allocatePreviewTarget()
    {
        EditorPreviewViewTarget preview;
        preview._id = _previewTargetIDAllocator.allocate();
        _listPreviewTarget.push_back( preview );
        return preview._id;
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
        EditorPreviewViewTarget* pPreview = findPreviewTargetMutable( id );
        if ( pPreview != nullptr )
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
        EditorPreviewViewTarget* pPreview = findPreviewTargetMutable( id );
        if ( pPreview == nullptr )
            return;
        pPreview->_sceneKey       = sceneKey;
        pPreview->_viewProj       = viewProj;
        pPreview->_cameraPos      = cameraPos;
        pPreview->_target._bDrawn = SW_TRUE;
        requestAnimationFrame(); // 보인 미리보기 씬은 이번 프레임에 틱한다(`ScenePreviewTick::WhenDrawn`) — 그 움직임을 그리려면 다음 프레임이 곧 와야 한다
    }

    uint32 EditorRenderViews::collectSceneViews( PreviewSceneView* pOutView, uint32 capacity ) const
    {
        uint32 count = 0;
        for ( const EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            const bool bVisible = preview._target._bDrawn == SW_TRUE && preview._target._renderTarget != 0 && preview._sceneKey != 0;
            if ( bVisible == false || count >= capacity )
                continue;
            PreviewSceneView& view     = pOutView[count];
            view                       = PreviewSceneView{};
            view._sceneKey             = preview._sceneKey;
            view._viewProj             = preview._viewProj;
            view._cameraPos            = preview._cameraPos;
            view._target._renderTarget = preview._target._renderTarget;
            view._target._width        = preview._target._width;
            view._target._height       = preview._target._height;
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
            view._pendingTexture = textures.request( view._pendingPath, AssetLoadStart::Async );
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
