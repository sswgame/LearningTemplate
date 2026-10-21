#include "pch.h"

#include "Editor/Common/Workspace/EditorRenderViews.h"

#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw::editor
{
    namespace
    {
        struct EditorRenderViewsInternal
        {
            /** @brief R8G8B8A8_UNORM 텍스처 전체를 한 색으로 업로드합니다. 업로드한 텍스처는 셰이더 읽기 상태입니다. */
            static bool fillTextureWithColor( IRHIResourceFactory& resource, RHITextureHandle texture, uint32 width, uint32 height, const float4& color )
            {
                // 메모리에서 R · G · B · A 순서가 되도록 낮은 바이트부터 채운다(엔진은 리틀 엔디언 64 비트만 빌드한다).
                const uint32 packedColor = static_cast<uint32>( toUnorm8( color._x ) ) | ( static_cast<uint32>( toUnorm8( color._y ) ) << 8 ) |
                                           ( static_cast<uint32>( toUnorm8( color._z ) ) << 16 ) | ( static_cast<uint32>( toUnorm8( color._w ) ) << 24 );
                const vector<uint32> listPixel( static_cast<size_t>( width ) * height, packedColor );

                RHITextureUploadDesc upload{};
                upload._pData     = listPixel.data();
                upload._sizeBytes = static_cast<uint32>( listPixel.size() * sizeof( uint32 ) );
                upload._mipLevels = 1;
                return resource.uploadTexture2D( texture, upload );
            }

            static uint8 toUnorm8( float32 value ) { return static_cast<uint8>( MathUtil::round( MathUtil::saturate( value ) * 255.0f ) ); }
        };
    } // namespace
} // namespace sw::editor

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
        preview._id = _nextPreviewTargetID++;
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

        const bool bCanReleaseRenderTarget = view._renderTarget != 0 && _pRHIDevice != nullptr && _pRHIDevice->getResourceFactory() != nullptr;
        if ( bCanReleaseRenderTarget )
        {
            // 줄 서 있는 패킷이 이 렌더 타깃에 그리고, 이미 낸 draw 스냅샷이 그것을 샘플링한다. ImGui 텍스처와 같은 큐에 맡겨 그 프레임들의
            // GPU 완료 뒤에 부순다. 렌더러 백엔드가 없으면(그릴 쪽이 없다) 곧바로 부순다.
            IRHIResourceFactory*   pResource    = _pRHIDevice->getResourceFactory();
            const RHITextureHandle renderTarget = view._renderTarget;
            if ( _pRendererBackend != nullptr )
            {
                _pRendererBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pResource, renderTarget]()
                { pResource->destroyTexture( renderTarget ); } ) );
            }
            else
            {
                pResource->destroyTexture( renderTarget );
            }
            view._renderTarget = 0;
        }

        view._width  = 0;
        view._height = 0;
    }

    void EditorRenderViews::ensureTargetSize( EditorViewTarget& view, uint32 width, uint32 height )
    {
        if ( width == 0 || height == 0 )
            return;
        const bool bRenderTargetUpToDate = width == view._width && height == view._height && view._renderTarget != 0;
        if ( bRenderTargetUpToDate )
            return;
        if ( _pRHIDevice == nullptr || _pRHIDevice->getResourceFactory() == nullptr )
            return;

        destroyTarget( view );

        // editortooldefaults.json 의 _clearColor 를 쓴다(값을 여기 박아 두면 설정 파일이 조용히 무시된다).
        const float4 viewClearColor = editor::getEditorToolDefaults()._clearColor;

        RHITextureDesc rtDesc{};
        rtDesc._width             = width;
        rtDesc._height            = height;
        rtDesc._format            = RHIFormat::R8G8B8A8_UNORM;
        rtDesc._bIsRenderTarget   = SW_TRUE;
        rtDesc._bIsShaderResource = SW_TRUE;
        rtDesc._mipLevels         = 1;
        rtDesc._clearColor        = viewClearColor;

        view._renderTarget = _pRHIDevice->getResourceFactory()->createTexture2D( rtDesc );
        if ( view._renderTarget == 0 )
            return;

        // 이번 프레임의 draw 스냅샷이 새 텍스처를 그리는데, 그 스냅샷은 이 렌더 타깃에 그릴 패킷보다 먼저 줄 선 패킷이 그릴 수 있다. 렌더러가 아직
        // 쓰지 않은 텍스처를 샘플링하지 않도록 클리어 색으로 채워 셰이더 읽기 상태로 둔다(Vulkan 은 UNDEFINED 레이아웃 샘플링이 검증 Error 다).
        // 닫혀 있다 열린 패널의 첫 프레임도 이 색이다(그 뷰는 보이는 동안만 그린다).
        if ( EditorRenderViewsInternal::fillTextureWithColor( *_pRHIDevice->getResourceFactory(), view._renderTarget, width, height, viewClearColor ) == false )
            SW_LOG_WARNING( "View target %#x%# could not be cleared before its first frame", width, height );

        view._width  = width;
        view._height = height;
        if ( _pRendererBackend != nullptr )
            view._pTextureID = _pRendererBackend->registerTexture( view._renderTarget );
    }
} // namespace sw::editor
