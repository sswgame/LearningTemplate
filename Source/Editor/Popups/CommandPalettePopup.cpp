#include "pch.h"

#include "Editor/Popups/CommandPalettePopup.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
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

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct CommandPalettePopupInternal
		{
			static bool fuzzyMatch( string_view text, string_view pattern )
			{
				if ( pattern.empty() )
					return true;
				if ( text.empty() )
					return false;

				size_t patternIdx = 0;
				for ( size_t textIdx = 0; textIdx < text.size(); ++textIdx )
				{
					const utf8 tc = StringUtil::toLowerChar( text[textIdx] );
					const utf8 pc = StringUtil::toLowerChar( pattern[patternIdx] );
					if ( tc == pc )
					{
						++patternIdx;
						if ( patternIdx == pattern.size() )
							return true;
					}
				}
				return false;
			}

			static void palettePlay()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() && EditorPlaySession::isStopped() )
				{
					pContext->getNotificationManager().push( "Play", "Scene has unsaved changes. Use Game View Play to confirm.",
															 NotificationType::Warning );
					return;
				}
				EditorPlaySession::play();
			}

			static void paletteSaveScene()
			{
				EditorAssetCommands::saveFocusedOrScene();
			}

			static void paletteExit()
			{
				EditorAssetCommands::requestExit();
			}

			static GameObject* palettePrimaryObject()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext == nullptr )
					return nullptr;
				return pContext->getSelectionManager().getPrimaryObject().get();
			}

			static Component* paletteSelectedComponent()
			{
				EditorContext* pContext = EditorContext::get();
				GameObject*	   pObj		= palettePrimaryObject();
				if ( pContext == nullptr || pObj == nullptr )
					return nullptr;
				const uint64 componentId = pContext->getWorkspace().getSelectedComponentId();
				if ( componentId == 0 )
					return nullptr;
				return pObj->findComponentById( componentId );
			}

			static void paletteWarn( const utf8* pTitle, const utf8* pDetail )
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
					pContext->getNotificationManager().push( pTitle, pDetail, NotificationType::Warning );
			}

			static void palettePasteValues()
			{
				EditorContext* pContext = EditorContext::get();
				Component*	   pComp	= paletteSelectedComponent();
				if ( pContext == nullptr || pComp == nullptr )
				{
					paletteWarn( "Paste", "Select a component first" );
					return;
				}
				if ( pContext->getWorkspace().hasCopiedComponent() == false )
				{
					paletteWarn( "Paste", "Clipboard is empty" );
					return;
				}
				pContext->getWorkspace().pasteComponentValues( pComp );
			}

			static void palettePasteAsNew()
			{
				EditorContext* pContext = EditorContext::get();
				GameObject*	   pObj		= palettePrimaryObject();
				if ( pContext == nullptr || pObj == nullptr )
				{
					paletteWarn( "Paste", "Select an object first" );
					return;
				}
				if ( pContext->getWorkspace().hasCopiedComponent() == false )
				{
					paletteWarn( "Paste", "Clipboard is empty" );
					return;
				}
				pContext->getWorkspace().pasteComponentAsNew( pObj );
			}

			static void onLoadPresetDialogResult( const vector<string>& listPaths )
			{
				if ( listPaths.empty() )
					return;
				Component* pComp = paletteSelectedComponent();
				if ( pComp == nullptr )
				{
					paletteWarn( "Preset", "Select a component first" );
					return;
				}
				EditorTransformCommands::loadComponentPreset( pComp, listPaths[0] );
			}

			static void onSavePresetDialogResult( const vector<string>& listPaths )
			{
				if ( listPaths.empty() )
					return;
				Component* pComp = paletteSelectedComponent();
				if ( pComp == nullptr )
				{
					paletteWarn( "Preset", "Select a component first" );
					return;
				}
				const string fileName = FileUtil::removeExtension( FileUtil::getFileNamePart( listPaths[0] ) );
				if ( fileName.empty() )
					return;
				EditorTransformCommands::saveComponentPreset( pComp, fileName );
			}

			static void paletteLoadPreset()
			{
				if ( paletteSelectedComponent() == nullptr )
				{
					paletteWarn( "Preset", "Select a component first" );
					return;
				}
				FileDialogParams params{};
				params._type				= FileDialogParams::Type::Open;
				params._title				= "Load Component Preset";
				params._description			= "Component Preset";
				params._bEnableMultiselect	= false;
				params._filterExtensionList = { ".preset.xml", ".xml" };
				params._initialDirectory	= EditorGlobalVariableCommands::getComponentPresetFolderPath();
				FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onLoadPresetDialogResult ) );
			}

			static void paletteSavePreset()
			{
				if ( paletteSelectedComponent() == nullptr )
				{
					paletteWarn( "Preset", "Select a component first" );
					return;
				}
				FileDialogParams params{};
				params._type				= FileDialogParams::Type::Save;
				params._title				= "Save Component Preset";
				params._description			= "Component Preset";
				params._bEnableMultiselect	= false;
				params._filterExtensionList = { ".preset.xml" };
				params._initialDirectory	= EditorGlobalVariableCommands::getComponentPresetFolderPath();
				FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onSavePresetDialogResult ) );
			}

			static void paletteDistributeX()
			{
				EditorTransformCommands::distributeSelectedObjects( AlignAxis::X );
			}

			static void paletteDistributeY()
			{
				EditorTransformCommands::distributeSelectedObjects( AlignAxis::Y );
			}

			static void paletteDistributeZ()
			{
				EditorTransformCommands::distributeSelectedObjects( AlignAxis::Z );
			}

			static void paletteAlignX()
			{
				EditorTransformCommands::alignSelectedObjects( AlignAxis::X, AlignType::Center );
			}

			static void paletteAlignY()
			{
				EditorTransformCommands::alignSelectedObjects( AlignAxis::Y, AlignType::Center );
			}

			static void paletteAlignZ()
			{
				EditorTransformCommands::alignSelectedObjects( AlignAxis::Z, AlignType::Center );
			}

			static void paletteSnapToGround()
			{
				EditorTransformCommands::snapSelectedToGround();
			}

			static void paletteApplyPrefab()
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
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
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
		{ CommandPalettePopupInternal::palettePlay(); } ) );
		registerCommandInstance( "Scene", "Save Scene", "Write the active scene, or prompt Save As if unsaved",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteSaveScene(); } ) );
		registerCommandInstance( "File", "Exit", "Close the editor after unsaved-change confirmation",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteExit(); } ) );
		registerCommandInstance( "Clipboard", "Paste Component Values", "Overwrite the selected component from the clipboard",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::palettePasteValues(); } ) );
		registerCommandInstance( "Clipboard", "Paste Component As New", "Add the copied component to the selected object",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::palettePasteAsNew(); } ) );
		registerCommandInstance( "Preset", "Load Component Preset", "Apply a .preset.xml to the selected component",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteLoadPreset(); } ) );
		registerCommandInstance( "Preset", "Save Component Preset", "Write the selected component to a preset file",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteSavePreset(); } ) );
		registerCommandInstance( "Transform", "Align X", "Align selected objects on X",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteAlignX(); } ) );
		registerCommandInstance( "Transform", "Align Y", "Align selected objects on Y",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteAlignY(); } ) );
		registerCommandInstance( "Transform", "Align Z", "Align selected objects on Z",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteAlignZ(); } ) );
		registerCommandInstance( "Transform", "Distribute X", "Evenly space selected objects on X",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteDistributeX(); } ) );
		registerCommandInstance( "Transform", "Distribute Y", "Evenly space selected objects on Y",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteDistributeY(); } ) );
		registerCommandInstance( "Transform", "Distribute Z", "Evenly space selected objects on Z",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteDistributeZ(); } ) );
		registerCommandInstance( "Transform", "Snap to Ground", "Snap selected objects onto the ground plane",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteSnapToGround(); } ) );
		registerCommandInstance( "Prefab", "Apply Overrides", "Write instance overrides back to the prefab template",
								 SW_DELEGATE_LAMBDA( Delegate<void()>, []()
		{ CommandPalettePopupInternal::paletteApplyPrefab(); } ) );
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
		_bJustOpened   = true;
		_selectedIndex = 0;
		_searchBuffer.clear();
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

		if ( EditorChrome::beginSearchOverlay( overlayDesc ) )
		{
			EditorWidgets::drawSearchField( "##PaletteSearch", _searchBuffer,
											"Type a command or search objects... (Esc to close)", -1.0f, false );

			ImGui::Separator();

			vector<const CommandPaletteEntry*> listFiltered;
			const string_view				   pattern{ _searchBuffer.c_str() };
			for ( const CommandPaletteEntry& entry : _listAllCommand )
			{
				if ( CommandPalettePopupInternal::fuzzyMatch( entry._label, pattern ) || CommandPalettePopupInternal::fuzzyMatch( entry._category, pattern ) ||
					 CommandPalettePopupInternal::fuzzyMatch( entry._detail, pattern ) )
				{
					listFiltered.push_back( &entry );
				}
			}

			const bool bExecuteSelected =
				EditorWidgets::updateListSelection( _selectedIndex, static_cast<int32>( listFiltered.size() ) );

			editor::EditorSectionDesc resultsDesc{};
			resultsDesc._pId   = "##PaletteResults";
			resultsDesc._kind  = editor::EditorSectionKind::Child;
			resultsDesc._flags = editor::EditorSectionFlags::Border;
			if ( EditorChrome::beginSection( resultsDesc ) )
			{
				for ( size_t itemIndex = 0; itemIndex < listFiltered.size(); ++itemIndex )
				{
					const CommandPaletteEntry& entry	   = *listFiltered[itemIndex];
					const bool				   bIsSelected = ( _selectedIndex == static_cast<int32>( itemIndex ) );

					ImGui::PushID( static_cast<int32>( itemIndex ) );

					fixed_string<constant::kMaxBuffer256> labelBuf;
					formatstring( labelBuf.data(), labelBuf.capacity(), "[%#] %#", entry._category.c_str(),
								  entry._label.c_str() );

					if ( ImGui::Selectable( labelBuf.c_str(), bIsSelected ) || ( bIsSelected && bExecuteSelected ) )
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
			EditorChrome::endSection();
		}
		EditorChrome::endSearchOverlay();
	}
} // namespace sw::editor
