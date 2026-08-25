#include "pch.h"

#include "Editor/Common/EditorContext.h"
#include "Editor/Overlay/CommandPaletteWindow.h"
#include "Editor/Windows/EditorWindowRegistry.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/EditorService.h"

#include <imgui.h>
#include <algorithm>

namespace sw
{
	namespace
	{
		CommandPaletteWindow* getImpl()
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
				const char tc = static_cast<char>( std::tolower( static_cast<unsigned char>( text[textIdx] ) ) );
				const char pc = static_cast<char>( std::tolower( static_cast<unsigned char>( pattern[patternIdx] ) ) );
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
	void CommandPaletteWindow::open()
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->openImpl();
	}

	void CommandPaletteWindow::close()
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->closeImpl();
	}

	void CommandPaletteWindow::toggle()
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->toggleImpl();
	}

	bool CommandPaletteWindow::isOpen()
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			return pWin->isOpenImpl();
		return false;
	}

	void CommandPaletteWindow::registerCommand( string_view category, string_view label, string_view detail,
												Delegate<void()> action )
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->registerCommandImpl( category, label, detail, std::move( action ) );
	}

	void CommandPaletteWindow::draw()
	{
		CommandPaletteWindow* pWin = getImpl();
		if ( pWin != nullptr )
			pWin->drawImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void CommandPaletteWindow::openImpl()
	{
		_bOpen				= true;
		_bJustOpened		= true;
		_selectedIndex		= 0;
		_arrSearchBuffer[0] = '\0';
		rebuildDynamicEntries();
	}

	void CommandPaletteWindow::closeImpl()
	{
		_bOpen = false;
	}

	void CommandPaletteWindow::toggleImpl()
	{
		if ( _bOpen )
			closeImpl();
		else
			openImpl();
	}

	void CommandPaletteWindow::registerCommandImpl( string_view category, string_view label, string_view detail,
													Delegate<void()> action )
	{
		CommandPaletteEntry entry;
		entry._category = string{ category };
		entry._label	= string{ label };
		entry._detail	= string{ detail };
		entry._action	= std::move( action );
		_listStaticCommands.push_back( std::move( entry ) );
	}

	void CommandPaletteWindow::rebuildDynamicEntries()
	{
		_listAllCommands = _listStaticCommands;

		// 1) 등록된 모든 에디터 윈도우 토글 커맨드
		for ( const EditorWindowEntry& win : EditorWindowRegistry::getWindows() )
		{
			const string		winTitle = win._title;
			CommandPaletteEntry entry;
			entry._category = "Window";
			entry._label	= "Open Window: " + winTitle;
			entry._detail	= "Editor Window";
			entry._action	= [winTitle]()
			{ EditorWindowRegistry::setWindowOpen( winTitle.c_str(), true ); };
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

	void CommandPaletteWindow::drawImpl()
	{
		if ( _bOpen == false )
			return;

		ImGuiViewport* pViewport = ImGui::GetMainViewport();
		const ImVec2   center	 = pViewport ? ImVec2{ pViewport->Pos.x + pViewport->Size.x * 0.5f,
													   pViewport->Pos.y + pViewport->Size.y * 0.28f }
											 : ImVec2{ 400.0f, 200.0f };

		constexpr float32 paletteWidth	= 580.0f;
		constexpr float32 paletteHeight = 360.0f;

		ImGui::SetNextWindowPos( center, ImGuiCond_Always, ImVec2{ 0.5f, 0.5f } );
		ImGui::SetNextWindowSize( ImVec2{ paletteWidth, paletteHeight } );

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
									   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 8.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.5f );
		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4{ 0.12f, 0.12f, 0.14f, 0.96f } );
		ImGui::PushStyleColor( ImGuiCol_Border, ImVec4{ 0.25f, 0.45f, 0.75f, 1.0f } );

		if ( ImGui::Begin( "##CommandPalette", &_bOpen, flags ) )
		{
			if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
				_bOpen = false;

			ImGui::SetNextItemWidth( -1.0f );
			if ( _bJustOpened )
			{
				ImGui::SetKeyboardFocusHere();
				_bJustOpened = false;
			}

			ImGui::InputTextWithHint( "##PaletteSearch", "Type a command or search objects... (Esc to close)",
									  _arrSearchBuffer, sizeof( _arrSearchBuffer ) );

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

			if ( ImGui::BeginChild( "##PaletteResults", ImVec2{ 0, 0 }, true ) )
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
			ImGui::EndChild();
		}
		ImGui::End();

		ImGui::PopStyleColor( 2 );
		ImGui::PopStyleVar( 2 );
	}
} // namespace sw
