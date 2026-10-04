/**
 * @file EditorContextLifecycle.cpp
 * @brief EditorContext 의 생성 · 초기화 · 종료입니다. 에디터 UI 매니저들을 실제로 만드는 곳입니다.
 *
 * @details EditorContext::get() 과 한 파일에 두지 말 것 — 그러면 **컨텍스트를 조회하기만 해도** 패널 · 팝업 ·
 *          인스펙터 매니저가 모두 링크에 끌려옵니다(그 끝은 ImGui 입니다). 생성 · 소멸(매니저 타입이 완전해야 하는 쪽)만
 *          이 TU 에 두어, EditorContext 를 조회만 하는 코드(EditorSceneCommands 단위 테스트 등)가 UI 없이 링크되게 합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw::editor
{
    namespace
    {
        struct EditorContextLifecycleInternal
        {
            /** @brief R8G8B8A8_UNORM 텍스처 전체를 한 색으로 올립니다. 올린 텍스처는 셰이더 읽기 상태입니다. */
            static bool fillTextureWithColor( IRHIResourceFactory& resource, RHITextureHandle texture, uint32 width, uint32 height, const float4& color )
            {
                // 메모리에서 R · G · B · A 순서가 되도록 낮은 바이트부터 채운다(엔진은 리틀 엔디언 64 비트만 짓는다).
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
    EditorContext::EditorContext()
        : _pRhiDevice{ nullptr }
        , _pRendererBackend{ nullptr }
        , _gameView{}
        , _bGameViewHovered{ SW_FALSE }
        , _bGameViewFocused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorContext::~EditorContext()
    {
        shutdown();
    }

    void EditorContext::initialize()
    {
        _pEditorSelection           = make_unique<EditorSelection>();
        _pWorkspace                 = make_unique<EditorWorkspace>( _pEditorSelection.get() );
        _pNotificationManager       = make_unique<EditorNotificationManager>();
        _pCommandRegistry           = make_unique<EditorCommandRegistry>();
        _pPanelManager              = make_unique<EditorPanelManager>();
        _pPopupManager              = make_unique<EditorPopupManager>();
        _pAssetHotReload            = make_unique<AssetHotReload>();
        _pAssetValidation           = make_unique<EditorAssetValidation>();
        _pSourceControl             = sw::make_unique<EditorSourceControl>( EditorUtil::getProjectRootPath() );
        _pInspectorComponentManager = make_unique<InspectorComponentManager>();
        _pInspectorPropertyManager  = make_unique<InspectorPropertyManager>();

        setActive( this );
        bindLocalService( this );

        _pInspectorComponentManager->registerDefaults();
        _pInspectorPropertyManager->registerDefaults();
        _pPopupManager->registerDefaultPopups();

        // 애셋 핫 리로드는 개발 기능이라 **에디터가 켜져 있을 때만** 감시가 돈다.
        // 리소스 루트가 없으면(팩만 실린 실행) 조용히 꺼진 채로 둔다.
        _pAssetHotReload->initialize();
        // 버전 관리는 git LFS 를 쓸 수 있는지 비동기로 묻는다 — 없으면 읽기 전용 표시만 한다.
        _pSourceControl->initialize();
    }

    void EditorContext::shutdown()
    {
        destroyGameView();

        if ( s_pActiveContext == this )
            setActive( nullptr );

        _pInspectorPropertyManager.reset();
        _pInspectorComponentManager.reset();
        _pSourceControl.reset();
        _pAssetValidation.reset();
        _pAssetHotReload.reset();
        _pPopupManager.reset();
        _pPanelManager.reset();
        _pCommandRegistry.reset();
        _pNotificationManager.reset();
        _pWorkspace.reset();
        _pEditorSelection.reset();
        _pRendererBackend = nullptr;
        _pRhiDevice       = nullptr;
        unbindLocalService<EditorContext>();
    }

    void EditorContext::destroyGameView()
    {
        if ( _gameView._pTextureId != nullptr && _pRendererBackend != nullptr )
        {
            _pRendererBackend->unregisterTexture( _gameView._pTextureId );
            _gameView._pTextureId = nullptr;
        }

        if ( _gameView._renderTarget != 0 && _pRhiDevice != nullptr && _pRhiDevice->getResourceFactory() != nullptr )
        {
            // 줄 서 있는 패킷이 이 렌더 타깃에 그리고, 이미 낸 draw 스냅샷이 그것을 샘플링한다. ImGui 텍스처와 같은 큐에 맡겨 그 프레임들의
            // GPU 완료 뒤에 부순다. 렌더러 백엔드가 없으면(그릴 쪽이 없다) 곧바로 부순다.
            IRHIResourceFactory*   pResource    = _pRhiDevice->getResourceFactory();
            const RHITextureHandle renderTarget = _gameView._renderTarget;
            if ( _pRendererBackend != nullptr )
            {
                _pRendererBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pResource, renderTarget]()
                { pResource->destroyTexture( renderTarget ); } ) );
            }
            else
            {
                pResource->destroyTexture( renderTarget );
            }
            _gameView._renderTarget = 0;
        }

        _gameView._width  = 0;
        _gameView._height = 0;
    }

    void EditorContext::ensureGameViewSize( uint32 width, uint32 height )
    {
        if ( width == 0 || height == 0 )
            return;
        if ( width == _gameView._width && height == _gameView._height && _gameView._renderTarget != 0 )
            return;
        if ( _pRhiDevice == nullptr || _pRhiDevice->getResourceFactory() == nullptr )
            return;

        destroyGameView();

        // editortooldefaults.json 의 _clearColor 를 쓴다(값을 여기 박아 두면 설정 파일이 조용히 무시된다).
        const float4 gameViewClearColor = editor::getEditorToolDefaults()._clearColor;

        RHITextureDesc rtDesc{};
        rtDesc._width             = width;
        rtDesc._height            = height;
        rtDesc._format            = RHIFormat::R8G8B8A8_UNORM;
        rtDesc._bIsRenderTarget   = SW_TRUE;
        rtDesc._bIsShaderResource = SW_TRUE;
        rtDesc._mipLevels         = 1;
        rtDesc._clearColor        = gameViewClearColor;

        _gameView._renderTarget = _pRhiDevice->getResourceFactory()->createTexture2D( rtDesc );
        if ( _gameView._renderTarget == 0 )
            return;

        // 이번 프레임의 draw 스냅샷이 새 텍스처를 그리는데, 그 스냅샷은 이 렌더 타깃에 그릴 패킷보다 먼저 줄 선 패킷이 그릴 수 있다. 렌더러가 아직
        // 쓰지 않은 텍스처를 샘플링하지 않도록 클리어 색으로 채워 셰이더 읽기 상태로 둔다(Vulkan 은 UNDEFINED 레이아웃 샘플링이 검증 Error 다).
        if ( EditorContextLifecycleInternal::fillTextureWithColor( *_pRhiDevice->getResourceFactory(), _gameView._renderTarget, width, height, gameViewClearColor ) == false )
            SW_LOG_WARNING( "Game view target %#x%# could not be cleared before its first frame", width, height );

        _gameView._width  = width;
        _gameView._height = height;
        if ( _pRendererBackend != nullptr )
            _gameView._pTextureId = _pRendererBackend->registerTexture( _gameView._renderTarget );
    }
} // namespace sw::editor
