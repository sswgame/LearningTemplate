#include "pch.h"

#include "Editor/Common/Workspace/EditorRenderViews.h"

#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorRenderViews" );

    void EditorRenderViews::shutdownSubsystem()
    {
        destroyViewTargets();
        _pRendererBackend = nullptr;
        _pRHIDevice       = nullptr;
    }

    void EditorRenderViews::clearViewDrawnMarks()
    {
        for ( EditorViewTarget& view : _arrViewTarget )
        {
            view._bDrawn = SW_FALSE;
        }
        for ( EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            preview._target._bDrawn = SW_FALSE;
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

    uint32 EditorRenderViews::collectSceneViews( HostSceneView* pOutView, uint32 capacity ) const
    {
        uint32 count = 0;
        for ( const EditorPreviewViewTarget& preview : _listPreviewTarget )
        {
            const bool bVisible = preview._target._bDrawn == SW_TRUE && preview._target._renderTarget != 0 && preview._sceneKey != 0;
            if ( bVisible == false || count >= capacity )
                continue;
            HostSceneView& view        = pOutView[count];
            view                       = HostSceneView{};
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

    void EditorRenderViews::destroyTarget( EditorViewTarget& view )
    {
        if ( view._pTextureID != nullptr && _pRendererBackend != nullptr )
        {
            _pRendererBackend->unregisterTexture( view._pTextureID );
            view._pTextureID = nullptr;
        }
        // 줄 서 있는 패킷이 이 렌더 타깃에 그리고, 이미 낸 draw 스냅샷이 그것을 샘플링한다 — 참조(로드 핸들)는 그리기 해제 큐에 맡겨 그 프레임들 뒤에 놓는다.
        // 마지막 참조가 사라지면 엔진이 GPU 자원을 놓는다. 그릴 쪽(렌더러 백엔드)이 없으면 지금 놓는다.
        if ( view._texture.isValid() && _pRendererBackend != nullptr )
        {
            auto pHold = sw::make_shared<TextureHandle>( std::move( view._texture ) );
            _pRendererBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pHold]()
            { *pHold = TextureHandle{}; } ) );
        }
        view._texture      = TextureHandle{};
        view._texturePath  = string{};
        view._renderTarget = 0;
        view._width        = 0;
        view._height       = 0;
    }

    void EditorRenderViews::ensureTargetSize( EditorViewTarget& view, uint32 width, uint32 height )
    {
        if ( width == 0 || height == 0 || _pRHIDevice == nullptr )
            return;
        TextureCache& textures = editor::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        if ( width != view._width || height != view._height || view._texture.isValid() == false )
        {
            destroyTarget( view );
            view._texturePath = formatToString( "%#editor/view%#", TextureCache::kRenderTargetPrefix, _nextTargetSerial++ );
            textures.declareRenderTarget( view._texturePath, width, height );
            view._texture = textures.request( view._texturePath, _pRHIDevice, AssetLoadStart::Async );
            view._width   = width;
            view._height  = height;
            if ( view._texture.isValid() == false )
            {
                SW_LOG_ERROR_ID( "EditorRenderViews.RequestFailed", "View target %#x%# could not be requested ('%#')", width, height, view._texturePath.c_str() );
                return;
            }
        }
        if ( view._renderTarget != 0 || view._texture.isReady() == false )
            return; // 이미 받았거나, 엔진이 아직 올리지 않았다(다음 프레임에 다시 본다)
        const Texture2D* pTexture = textures.find( view._texturePath );
        if ( pTexture == nullptr || pTexture->isRHIValid() == false )
            return;
        view._renderTarget = pTexture->getHandle();
        if ( _pRendererBackend != nullptr )
            view._pTextureID = _pRendererBackend->registerTexture( view._renderTarget );
    }
} // namespace sw::editor
