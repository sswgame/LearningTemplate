#include "pch.h"

#include "Editor/Common/EditorContext.h"

#include "Editor/Overlay/CommandPaletteWindow.h"
#include "Editor/Overlay/EditorNotificationManager.h"
#include "Editor/Property/ComponentDrawerRegistry.h"
#include "Editor/Property/IComponentDrawer.h"
#include "Editor/Property/IPropertyDrawer.h"
#include "Editor/Property/PropertyDrawerRegistry.h"
#include "Editor/Windows/EditorWindowRegistry.h"
#include "Editor/Workspace/AssetEditorRegistry.h"
#include "Editor/Workspace/EditorContextMenuRegistry.h"
#include "Editor/Workspace/SelectionManager.h"

namespace sw
{
	EditorContext* EditorContext::s_pActiveContext = nullptr;

	EditorContext::EditorContext()
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
		_pCommandPalette		  = make_unique<CommandPaletteWindow>();
		_pWindowRegistry		  = make_unique<EditorWindowRegistry>();
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
		if ( s_pActiveContext == this )
			setActive( nullptr );

		_pPropertyDrawerRegistry.reset();
		_pComponentDrawerRegistry.reset();
		_pAssetEditorRegistry.reset();
		_pWindowRegistry.reset();
		_pCommandPalette.reset();
		_pContextMenuRegistry.reset();
		_pNotificationManager.reset();
		_pSelectionManager.reset();
	}
} // namespace sw
