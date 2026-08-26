#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"

#include "Editor/Common/Platform/IImGuiRendererBackend.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/EditorNotificationManager.h"
#include "Editor/Inspector/ComponentDrawerRegistry.h"
#include "Editor/Inspector/IComponentDrawer.h"
#include "Editor/Inspector/IPropertyDrawer.h"
#include "Editor/Inspector/PropertyDrawerRegistry.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Editor/Common/Workspace/AssetEditorRegistry.h"
#include "Editor/Common/Workspace/EditorContextMenuRegistry.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
	EditorContext* EditorContext::s_pActiveContext = nullptr;

	EditorContext::EditorContext()
		: _pRhiDevice{ nullptr }
		, _pRendererBackend{ nullptr }
		, _gameView{}
		, _bGameViewHovered{ 0 }
		, _bGameViewFocused{ 0 }
		, _reserved{ 0 }
	{
	}

	EditorContext::~EditorContext()
	{
		shutdown();
	}

	void EditorContext::initialize()
	{
		_pSelectionManager		  = make_unique<SelectionManager>();
		_pNotificationManager	  = make_unique<EditorNotificationManager>();
		_pContextMenuRegistry	  = make_unique<EditorContextMenuRegistry>();
		_pCommandPalette		  = make_unique<CommandPalettePopup>();
		_pPanelRegistry		  = make_unique<EditorPanelRegistry>();
		_pAssetEditorRegistry	  = make_unique<AssetEditorRegistry>();
		_pComponentDrawerRegistry = make_unique<ComponentDrawerRegistry>();
		_pPropertyDrawerRegistry  = make_unique<PropertyDrawerRegistry>();

		setActive( this );

		// 기본 드로어 및 매핑 등록
		_pAssetEditorRegistry->registerDefaultMappings();
		_pComponentDrawerRegistry->registerDefaultDrawers();
		_pPropertyDrawerRegistry->registerDefaultDrawers();
	}

	void EditorContext::shutdown()
	{
		destroyGameView();

		if ( s_pActiveContext == this )
			setActive( nullptr );

		_pPropertyDrawerRegistry.reset();
		_pComponentDrawerRegistry.reset();
		_pAssetEditorRegistry.reset();
		_pPanelRegistry.reset();
		_pCommandPalette.reset();
		_pContextMenuRegistry.reset();
		_pNotificationManager.reset();
		_pSelectionManager.reset();
		_pRendererBackend = nullptr;
		_pRhiDevice		  = nullptr;
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

		constexpr float32 kGameViewClearColor[4] = { 0.12f, 0.15f, 0.18f, 1.0f };

		RHITextureDesc rtDesc{};
		rtDesc._width			  = width;
		rtDesc._height			  = height;
		rtDesc._format			  = RHIFormat::R8G8B8A8_UNORM;
		rtDesc._bIsRenderTarget	  = true;
		rtDesc._bIsShaderResource = true;
		rtDesc._mipLevels		  = 1;
		rtDesc._arrClearColor[0]  = kGameViewClearColor[0];
		rtDesc._arrClearColor[1]  = kGameViewClearColor[1];
		rtDesc._arrClearColor[2]  = kGameViewClearColor[2];
		rtDesc._arrClearColor[3]  = kGameViewClearColor[3];

		_gameView._renderTarget = _pRhiDevice->getResource()->createTexture2D( rtDesc );
		if ( _gameView._renderTarget == 0 )
			return;

		_gameView._width  = width;
		_gameView._height = height;
		if ( _pRendererBackend != nullptr )
			_gameView._pTextureId = _pRendererBackend->registerTexture( _gameView._renderTarget );
	}
} // namespace sw
