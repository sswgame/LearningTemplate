#include "pch.h"

#include "Editor/Popups/CommandPalettePopup.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <algorithm>

namespace sw::editor
{
	namespace
	{
		bool fuzzyMatch( string_view text, string_view pattern )
		{
			if ( pattern.empty() )
				return true;
			if ( text.empty() )
				return false;

			size_t patternIdx = 0;
			for ( size_t textIdx = 0; textIdx < text.size(); ++textIdx )
			{
				const utf8 tc = static_cast<utf8>( std::tolower( static_cast<uint8>( text[textIdx] ) ) );
				const utf8 pc = static_cast<utf8>( std::tolower( static_cast<uint8>( pattern[patternIdx] ) ) );
				if ( tc == pc )
				{
					++patternIdx;
					if ( patternIdx == pattern.size() )
						return true;
				}
			}
			return false;
		}

		void palettePlay()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() && EditorPlaySession::isStopped() )
				pContext->getNotificationManager().push( "Play", "Scene has unsaved changes", NotificationType::Warning );
			EditorPlaySession::play();
		}

		void paletteSaveScene()
		{
			EditorContext* pContext = EditorContext::get();
			if ( EditorAssetCommands::saveActiveScene( {} ) )
			{
				if ( pContext != nullptr )
					pContext->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
			}
			else if ( pContext != nullptr )
				pContext->getNotificationManager().push( "Scene", "Save failed — use File > Save Scene As", NotificationType::Warning );
		}

		void paletteAlignX()
		{
			EditorTransformCommands::alignSelectedObjects( AlignAxis::X, AlignType::Center );
		}

		void paletteAlignY()
		{
			EditorTransformCommands::alignSelectedObjects( AlignAxis::Y, AlignType::Center );
		}

		void paletteAlignZ()
		{
			EditorTransformCommands::alignSelectedObjects( AlignAxis::Z, AlignType::Center );
		}

		void paletteSnapToGround()
		{
			EditorTransformCommands::snapSelectedToGround();
		}

		void paletteApplyPrefab()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext == nullptr )
				return;
			GameObject* pObj = pContext->getSelectionManager().getPrimaryObject().get();
			string		path = pContext->getWorkspace().getFocusedAssetPath();
			if ( path.empty() && pObj != nullptr )
				path = pContext->getWorkspace().getGameObjectPrefabPath( pObj->getObjectId() );
			EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, path );
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Constructor
	// ------------------------------------------------------------------------------
	CommandPalettePopup::CommandPalettePopup()
		: IEditorPopup{ false }
		, _listStaticCommand{}
		, _listAllCommand{}
		, _selectedIndex{ 0 }
		, _bJustOpened{ false }
	{
		registerCommandInstance( "Play", "Play", "Start play-in-editor",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ palettePlay(); } ) );
		registerCommandInstance( "Scene", "Save Scene", "Write the active scene to its source path",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteSaveScene(); } ) );
		registerCommandInstance( "Transform", "Align X", "Align selected objects on X",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteAlignX(); } ) );
		registerCommandInstance( "Transform", "Align Y", "Align selected objects on Y",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteAlignY(); } ) );
		registerCommandInstance( "Transform", "Align Z", "Align selected objects on Z",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteAlignZ(); } ) );
		registerCommandInstance( "Transform", "Snap to Ground", "Snap selected objects onto the ground plane",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteSnapToGround(); } ) );
		registerCommandInstance( "Prefab", "Apply Overrides", "Write instance overrides back to the prefab template",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ paletteApplyPrefab(); } ) );
	}

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void CommandPalettePopup::open()
	{
		EditorContext::get()->getPopupManager().openPopup( "CommandPalette" );
	}

	void CommandPalettePopup::close()
	{
		EditorContext::get()->getPopupManager().closePopup( "CommandPalette" );
	}

	void CommandPalettePopup::toggle()
	{
		EditorContext::get()->getPopupManager().togglePopup( "CommandPalette" );
	}

	bool CommandPalettePopup::isOpen()
	{
		return EditorContext::get()->getPopupManager().isPopupOpen( "CommandPalette" );
	}

	void CommandPalettePopup::registerCommand( string_view category, string_view label, string_view detail,
											   Delegate<void()> action )
	{
		CommandPalettePopup* pPopup = EditorContext::get()->getPopupManager().findPopup<CommandPalettePopup>( "CommandPalette" );
		if ( pPopup != nullptr )
			pPopup->registerCommandInstance( category, label, detail, std::move( action ) );
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void CommandPalettePopup::registerCommandInstance( string_view category, string_view label, string_view detail,
													   Delegate<void()> action )
	{
		CommandPaletteEntry entry;
		entry._category = string{ category };
		entry._label	= string{ label };
		entry._detail	= string{ detail };
		entry._action	= std::move( action );
		_listStaticCommand.push_back( std::move( entry ) );
	}

	void CommandPalettePopup::onOpen()
	{
		_bJustOpened		= true;
		_selectedIndex		= 0;
		_arrSearchBuffer[0] = '\0';
		rebuildDynamicEntries();
	}

	void CommandPalettePopup::rebuildDynamicEntries()
	{
		_listAllCommand = _listStaticCommand;

		// 1) 등록된 모든 에디터 패널 토글 커맨드
		for ( const EditorPanelEntry& win : EditorContext::get()->getPanelManager().getPanels() )
		{
			const string		winTitle = win._title;
			CommandPaletteEntry entry;
			entry._category = "Panel";
			entry._label	= "Open Panel: " + winTitle;
			entry._detail	= "Editor Panel";
			entry._action	= [winTitle]()
			{ EditorContext::get()->getPanelManager().setPanelOpen( winTitle.c_str(), true ); };
			_listAllCommand.push_back( std::move( entry ) );
		}

		// 2) 씬 내 게임오브젝트 검색 커맨드
		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager != nullptr )
		{
			Scene* pScene = pSceneManager->getActiveScene();
			if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
			{
				for ( GameObject* pObj : pScene->getObjectManager()->getAllGameObjects() )
				{
					if ( pObj == nullptr )
						continue;

					const uint64 objId	 = pObj->getObjectId();
					const string objName = string{ pObj->getName().c_str() };

					CommandPaletteEntry entry;
					entry._category = "GameObject";
					entry._label	= "Select GameObject: " + objName;
					entry._detail	= "Scene Object (ID: " + to_string( objId ) + ")";
					entry._action	= [objId]()
					{
						SceneManager* pMgr = editor::getService<SceneManager>();
						if ( pMgr && pMgr->getActiveScene() && pMgr->getActiveScene()->getObjectManager() )
						{
							GameObject* pFound = pMgr->getActiveScene()->getObjectManager()->findGameObjectById( objId );
							if ( pFound )
								EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pFound }, SelectionMode::Replace );
						}
					};
					_listAllCommand.push_back( std::move( entry ) );
				}
			}
		}
	}

	void CommandPalettePopup::drawContent()
	{
		editor::EditorSearchOverlayDesc overlayDesc{};
		overlayDesc._pId		  = "##CommandPalette";
		overlayDesc._pOpen		  = &_bOpen;
		overlayDesc._size		  = float2{ 580.0f, 360.0f };
		overlayDesc._pFocusOnOpen = &_bJustOpened;

		if ( editor::beginSearchOverlay( overlayDesc ) )
		{
			editor::drawSearchField( "##PaletteSearch", _arrSearchBuffer, sizeof( _arrSearchBuffer ),
									 "Type a command or search objects... (Esc to close)", -1.0f, false );

			ImGui::Separator();

			vector<const CommandPaletteEntry*> listFiltered;
			const string_view				   pattern{ _arrSearchBuffer };
			for ( const CommandPaletteEntry& entry : _listAllCommand )
			{
				if ( fuzzyMatch( entry._label, pattern ) || fuzzyMatch( entry._category, pattern ) ||
					 fuzzyMatch( entry._detail, pattern ) )
				{
					listFiltered.push_back( &entry );
				}
			}

			const bool bExecuteSelected =
				editor::updateListSelection( _selectedIndex, static_cast<int32>( listFiltered.size() ) );

			editor::EditorSectionDesc resultsDesc{};
			resultsDesc._pId   = "##PaletteResults";
			resultsDesc._kind  = editor::EditorSectionKind::Child;
			resultsDesc._flags = editor::EditorSectionFlags::Border;
			if ( editor::beginSection( resultsDesc ) )
			{
				for ( size_t itemIndex = 0; itemIndex < listFiltered.size(); ++itemIndex )
				{
					const CommandPaletteEntry& entry	   = *listFiltered[itemIndex];
					const bool				   bIsSelected = ( _selectedIndex == static_cast<int32>( itemIndex ) );

					ImGui::PushID( static_cast<int32>( itemIndex ) );

					utf8 arrLabelBuf[256];
					formatstring( arrLabelBuf, sizeof( arrLabelBuf ), "[%#] %#", entry._category.c_str(),
								  entry._label.c_str() );

					if ( ImGui::Selectable( arrLabelBuf, bIsSelected ) || ( bIsSelected && bExecuteSelected ) )
					{
						if ( entry._action.isBound() )
							entry._action();
						_bOpen = false;
						ImGui::PopID();
						break;
					}

					if ( entry._detail.empty() == false )
					{
						ImGui::SameLine();
						ImGui::SetCursorPosX( overlayDesc._size._x - 180.0f );
						ImGui::TextDisabled( "%s", entry._detail.c_str() );
					}

					ImGui::PopID();
				}
			}
			editor::endSection();
		}
		editor::endSearchOverlay();
	}
} // namespace sw::editor
