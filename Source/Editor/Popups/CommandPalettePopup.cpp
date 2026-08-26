#include "pch.h"

#include "Editor/Popups/CommandPalettePopup.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Panels/EditorPanelRegistry.h"

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
		CommandPalettePopup* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getCommandPalette();
			return nullptr;
		}

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
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void CommandPalettePopup::open()
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->openImpl();
	}

	void CommandPalettePopup::close()
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->closeImpl();
	}

	void CommandPalettePopup::toggle()
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->toggleImpl();
	}

	bool CommandPalettePopup::isOpen()
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			return pWin->isOpenImpl();
		return false;
	}

	void CommandPalettePopup::registerCommand( string_view category, string_view label, string_view detail,
											   Delegate<void()> action )
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->registerCommandImpl( category, label, detail, std::move( action ) );
	}

	void CommandPalettePopup::draw()
	{
		CommandPalettePopup* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->drawImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void CommandPalettePopup::openImpl()
	{
		_bOpen				= true;
		_bJustOpened		= true;
		_selectedIndex		= 0;
		_arrSearchBuffer[0] = '\0';
		rebuildDynamicEntries();
	}

	void CommandPalettePopup::closeImpl()
	{
		_bOpen = false;
	}

	void CommandPalettePopup::toggleImpl()
	{
		if ( _bOpen )
			closeImpl();
		else
			openImpl();
	}

	void CommandPalettePopup::registerCommandImpl( string_view category, string_view label, string_view detail,
												   Delegate<void()> action )
	{
		CommandPaletteEntry entry;
		entry._category = string{ category };
		entry._label	= string{ label };
		entry._detail	= string{ detail };
		entry._action	= std::move( action );
		_listStaticCommands.push_back( std::move( entry ) );
	}

	void CommandPalettePopup::rebuildDynamicEntries()
	{
		_listAllCommands = _listStaticCommands;

		// 1) 등록된 모든 에디터 패널 토글 커맨드
		for ( const EditorPanelEntry& win : EditorPanelRegistry::getPanels() )
		{
			const string		winTitle = win._title;
			CommandPaletteEntry entry;
			entry._category = "Panel";
			entry._label	= "Open Panel: " + winTitle;
			entry._detail	= "Editor Panel";
			entry._action	= [winTitle]()
			{ EditorPanelRegistry::setPanelOpen( winTitle.c_str(), true ); };
			_listAllCommands.push_back( std::move( entry ) );
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
								SelectionManager::selectObject( GameObjectPtr{ pFound }, SelectionMode::Replace );
						}
					};
					_listAllCommands.push_back( std::move( entry ) );
				}
			}
		}
	}

	void CommandPalettePopup::drawImpl()
	{
		if ( _bOpen == false )
			return;

		float2 viewportPos{};
		float2 viewportSize{};
		if ( editor::tryGetMainViewportRect( viewportPos, viewportSize ) == false )
		{
			viewportPos	 = float2{ 0.0f, 0.0f };
			viewportSize = float2{ 800.0f, 600.0f };
		}

		constexpr float32 paletteWidth	= 580.0f;
		constexpr float32 paletteHeight = 360.0f;

		editor::EditorOverlayDesc overlayDesc{};
		overlayDesc._pId		= "##CommandPalette";
		overlayDesc._pOpen		= &_bOpen;
		overlayDesc._anchorPos	= float2{ viewportPos._x + viewportSize._x * 0.5f, viewportPos._y + viewportSize._y * 0.28f };
		overlayDesc._pivot		= float2{ 0.5f, 0.5f };
		overlayDesc._size		= float2{ paletteWidth, paletteHeight };
		overlayDesc._rounding	= 8.0f;
		overlayDesc._borderSize = 1.5f;
		overlayDesc._flags		= editor::EditorOverlayFlags::NoTitleBar | editor::EditorOverlayFlags::NoResize |
								  editor::EditorOverlayFlags::NoMove | editor::EditorOverlayFlags::NoSavedSettings;

		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4{ 0.12f, 0.12f, 0.14f, 0.96f } );
		ImGui::PushStyleColor( ImGuiCol_Border, ImVec4{ 0.25f, 0.45f, 0.75f, 1.0f } );

		if ( editor::beginOverlay( overlayDesc ) )
		{
			if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
				_bOpen = false;

			if ( _bJustOpened )
			{
				ImGui::SetKeyboardFocusHere();
				_bJustOpened = false;
			}

			editor::drawSearchField( "##PaletteSearch", _arrSearchBuffer, sizeof( _arrSearchBuffer ),
									 "Type a command or search objects... (Esc to close)", -1.0f, false );

			ImGui::Separator();

			// 필터링된 커맨드 목록 수집
			vector<const CommandPaletteEntry*> listFiltered;
			const string_view				   pattern{ _arrSearchBuffer };
			for ( const CommandPaletteEntry& entry : _listAllCommands )
			{
				if ( fuzzyMatch( entry._label, pattern ) || fuzzyMatch( entry._category, pattern ) ||
					 fuzzyMatch( entry._detail, pattern ) )
				{
					listFiltered.push_back( &entry );
				}
			}

			if ( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
			{
				++_selectedIndex;
				if ( _selectedIndex >= static_cast<int32>( listFiltered.size() ) )
					_selectedIndex = static_cast<int32>( listFiltered.size() ) - 1;
			}
			if ( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
			{
				--_selectedIndex;
				if ( _selectedIndex < 0 )
					_selectedIndex = 0;
			}

			const bool bExecuteSelected = ImGui::IsKeyPressed( ImGuiKey_Enter );

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
						ImGui::SetCursorPosX( paletteWidth - 180.0f );
						ImGui::TextDisabled( "%s", entry._detail.c_str() );
					}

					ImGui::PopID();
				}
			}
			editor::endSection();
		}
		editor::endOverlay();

		ImGui::PopStyleColor( 2 );
	}
} // namespace sw::editor
