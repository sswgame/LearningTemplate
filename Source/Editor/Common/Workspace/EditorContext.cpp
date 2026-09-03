#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/EditorActionMenuManager.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
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
    EditorContext* EditorContext::s_pActiveContext = nullptr;

    EditorContext* EditorContext::get()
    {
        EditorContext* pLocalContext = getService<EditorContext>();
        return pLocalContext != nullptr ? pLocalContext : s_pActiveContext;
    }

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
        _pPanelManager              = make_unique<EditorPanelManager>();
        _pPopupManager              = make_unique<EditorPopupManager>();
        _pAssetEditorManager        = make_unique<AssetEditorManager>();
        _pInspectorComponentManager = make_unique<InspectorComponentManager>();
        _pInspectorPropertyManager  = make_unique<InspectorPropertyManager>();

        setActive( this );
        bindLocalService( this );

        _pAssetEditorManager->registerDefaultMappings();
        _pInspectorComponentManager->registerDefaults();
        _pInspectorPropertyManager->registerDefaults();
        _pPopupManager->registerDefaultPopups();
    }

    void EditorContext::shutdown()
    {
        destroyGameView();

        if ( s_pActiveContext == this )
            setActive( nullptr );

        _pInspectorPropertyManager.reset();
        _pInspectorComponentManager.reset();
        _pAssetEditorManager.reset();
        _pPopupManager.reset();
        _pPanelManager.reset();
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

        constexpr float4 kGameViewClearColor{ 0.12f, 0.15f, 0.18f, 1.0f };

        RHITextureDesc rtDesc{};
        rtDesc._width             = width;
        rtDesc._height            = height;
        rtDesc._format            = RHIFormat::R8G8B8A8_UNORM;
        rtDesc._bIsRenderTarget   = SW_TRUE;
        rtDesc._bIsShaderResource = SW_TRUE;
        rtDesc._mipLevels         = 1;
        rtDesc._clearColor        = kGameViewClearColor;

        _gameView._renderTarget = _pRhiDevice->getResource()->createTexture2D( rtDesc );
        if ( _gameView._renderTarget == 0 )
            return;

        _gameView._width  = width;
        _gameView._height = height;
        if ( _pRendererBackend != nullptr )
            _gameView._pTextureId = _pRendererBackend->registerTexture( _gameView._renderTarget );
    }
} // namespace sw::editor
