/**
 * @file EditorContextLifecycle.cpp
 * @brief EditorContext 의 생성·초기화·종료 — 에디터 UI 매니저들을 실제로 만드는 곳
 *
 * @details EditorContext::get() 과 한 파일에 있었는데, 그러면 **컨텍스트를 조회하기만 해도**
 *          패널·팝업·인스펙터 매니저가 전부 링크에 끌려온다(그 끝은 ImGui 다). 조회는 포인터
 *          하나를 돌려주는 일이고 매니저를 알 필요가 없다. 생성·소멸(=매니저 타입이 완전해야
 *          하는 쪽)만 이 TU 로 갈라, EditorContext 를 조회만 하는 코드가 UI 없이 링크되게 한다
 *          — EditorSceneCommands 단위 테스트가 그래서 가능해졌다.
 */
#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Gui/EditorActionMenuManager.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

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
        _pSelectionManager          = make_unique<SelectionManager>();
        _pWorkspace                 = make_unique<EditorWorkspace>( _pSelectionManager.get() );
        _pNotificationManager       = make_unique<EditorNotificationManager>();
        _pActionMenuManager         = make_unique<EditorActionMenuManager>();
        _pCommandRegistry           = make_unique<EditorCommandRegistry>();
        _pPanelManager              = make_unique<EditorPanelManager>();
        _pPopupManager              = make_unique<EditorPopupManager>();
        _pAssetEditorManager        = make_unique<AssetEditorManager>();
        _pAssetHotReload            = make_unique<AssetHotReload>();
        _pInspectorComponentManager = make_unique<InspectorComponentManager>();
        _pInspectorPropertyManager  = make_unique<InspectorPropertyManager>();

        setActive( this );
        bindLocalService( this );

        _pAssetEditorManager->registerDefaultMappings();
        _pInspectorComponentManager->registerDefaults();
        _pInspectorPropertyManager->registerDefaults();
        _pPopupManager->registerDefaultPopups();

        // 에셋 핫리로드는 개발 기능이라 **에디터가 켜져 있을 때만** 감시가 돈다.
        // 리소스 루트가 없으면(팩만 실린 실행) 조용히 꺼진 채로 둔다.
        _pAssetHotReload->initialize();
    }

    void EditorContext::shutdown()
    {
        destroyGameView();

        if ( s_pActiveContext == this )
            setActive( nullptr );

        _pInspectorPropertyManager.reset();
        _pInspectorComponentManager.reset();
        _pAssetHotReload.reset();
        _pAssetEditorManager.reset();
        _pPopupManager.reset();
        _pPanelManager.reset();
        _pCommandRegistry.reset();
        _pActionMenuManager.reset();
        _pNotificationManager.reset();
        _pWorkspace.reset();
        _pSelectionManager.reset();
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

        if ( _gameView._renderTarget != 0 && _pRhiDevice != nullptr && _pRhiDevice->getResource() != nullptr )
        {
            _pRhiDevice->getResource()->destroyTexture( _gameView._renderTarget );
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
        if ( _pRhiDevice == nullptr || _pRhiDevice->getResource() == nullptr )
            return;

        destroyGameView();

        // editordata.json 의 _clearColor 를 쓴다. 예전에는 여기에 같은 값을 손으로 박아 두어
        // XML 을 고쳐도 아무 일도 일어나지 않았다 (설정이 조용히 무시되는 자리였다).
        const float4 gameViewClearColor = editor::getEditorData()._clearColor;

        RHITextureDesc rtDesc{};
        rtDesc._width             = width;
        rtDesc._height            = height;
        rtDesc._format            = RHIFormat::R8G8B8A8_UNORM;
        rtDesc._bIsRenderTarget   = SW_TRUE;
        rtDesc._bIsShaderResource = SW_TRUE;
        rtDesc._mipLevels         = 1;
        rtDesc._clearColor        = gameViewClearColor;

        _gameView._renderTarget = _pRhiDevice->getResource()->createTexture2D( rtDesc );
        if ( _gameView._renderTarget == 0 )
            return;

        _gameView._width  = width;
        _gameView._height = height;
        if ( _pRendererBackend != nullptr )
            _gameView._pTextureId = _pRendererBackend->registerTexture( _gameView._renderTarget );
    }
} // namespace sw::editor
