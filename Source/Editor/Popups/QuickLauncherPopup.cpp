#include "pch.h"

#include "Editor/Popups/QuickLauncherPopup.h"

#include "Core/Common/StdHeaders.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct QuickLauncherPopupInternal
		{
			static ImVec4 getCategoryColor( string_view cat )
			{
				if ( cat == "Scene" )
					return ImVec4( 0.25f, 0.85f, 0.45f, 1.0f );
				if ( cat == "Prefab" )
					return ImVec4( 0.30f, 0.65f, 1.0f, 1.0f );
				if ( cat == "Texture" )
					return ImVec4( 0.95f, 0.65f, 0.25f, 1.0f );
				if ( cat == "Shader" )
					return ImVec4( 0.85f, 0.40f, 0.95f, 1.0f );
				if ( cat == "GameObject" )
					return ImVec4( 0.95f, 0.85f, 0.30f, 1.0f );
				return ImVec4( 0.60f, 0.65f, 0.75f, 1.0f );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	QuickLauncherPopup::QuickLauncherPopup()
		: _listAllItem{}
		, _fileIndexJob{}
		, _searchBuffer{}
		, _selectedIndex{ 0 }
		, _bJustOpened{ false }
	{
	}

	void QuickLauncherPopup::open()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getPopupManager().openPopup( "QuickLauncher" );
	}

	void QuickLauncherPopup::close()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getPopupManager().closePopup( "QuickLauncher" );
	}

	void QuickLauncherPopup::toggle()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getPopupManager().togglePopup( "QuickLauncher" );
	}

	bool QuickLauncherPopup::isOpen()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			return pContext->getPopupManager().isPopupOpen( "QuickLauncher" );
		return false;
	}

	void QuickLauncherPopup::onOpen()
	{
		_bJustOpened   = true;
		_selectedIndex = 0;
		_searchBuffer.clear();
		rebuildIndex();
	}

	void QuickLauncherPopup::rebuildIndex()
	{
		_listAllItem.clear();

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

					const uint64	  objId = pObj->getObjectId();
					QuickLauncherItem item{};
					item._category = "GameObject";
					item._title	   = string{ pObj->getName().c_str() };

					StringBuilder<constant::kMaxBuffer64> detailSb;
					detailSb.appendFormat( "Scene GameObject (ID: %#)", objId );
					item._detail		 = string{ detailSb.view() };
					item._targetObjectId = objId;
					_listAllItem.push_back( std::move( item ) );
				}
			}
		}

		_fileIndexJob.request();
	}

	void QuickLauncherPopup::pollFileIndex()
	{
		vector<EditorResourceIndexEntry> listFileEntry;
		if ( _fileIndexJob.take( listFileEntry ) == false )
			return;

		for ( const EditorResourceIndexEntry& entry : listFileEntry )
		{
			QuickLauncherItem item{};
			item._category = entry._category;
			item._title	   = entry._title;
			item._detail   = entry._detail;
			item._path	   = entry._path;
			_listAllItem.push_back( std::move( item ) );
		}
	}

	void QuickLauncherPopup::executeItem( const QuickLauncherItem& item )
	{
		if ( item._category == "GameObject" )
		{
			SceneManager* pMgr = editor::getService<SceneManager>();
			if ( pMgr == nullptr || pMgr->getActiveScene() == nullptr ||
				 pMgr->getActiveScene()->getObjectManager() == nullptr )
				return;

			GameObject* pFound = pMgr->getActiveScene()->getObjectManager()->findGameObjectById( item._targetObjectId );
			EditorSceneCommands::select( pFound, SelectionMode::Replace );
			return;
		}

		if ( item._category == "Scene" )
		{
			EditorAssetCommands::tryOpenScene( item._path );
			return;
		}

		EditorAssetCommands::focusPath( item._path );
	}

	void QuickLauncherPopup::drawContent()
	{
		pollFileIndex();

		editor::EditorSearchOverlayDesc overlayDesc{};
		overlayDesc._pId		  = "##QuickLauncherOverlay";
		overlayDesc._pOpen		  = &_bOpen;
		overlayDesc._size		  = float2{ 620.0f, 400.0f };
		overlayDesc._borderColor  = float4{ 0.25f, 0.55f, 0.85f, 1.0f };
		overlayDesc._pFocusOnOpen = &_bJustOpened;

		if ( EditorChrome::beginSearchOverlay( overlayDesc ) == false )
		{
			EditorChrome::endSearchOverlay();
			close();
			return;
		}

		EditorWidgets::drawSearchField( "##qlSearch", _searchBuffer,
										"Type to search assets, scenes, game objects (ESC to cancel)...", -1.0f, true );

		vector<const QuickLauncherItem*> listFiltered;
		listFiltered.reserve( _listAllItem.size() );

		for ( const QuickLauncherItem& item : _listAllItem )
		{
			if ( _searchBuffer.empty() )
			{
				listFiltered.push_back( &item );
				continue;
			}

			if ( StringUtil::stristr( item._title.c_str(), _searchBuffer.c_str() ) != nullptr ||
				 StringUtil::stristr( item._detail.c_str(), _searchBuffer.c_str() ) != nullptr ||
				 StringUtil::stristr( item._category.c_str(), _searchBuffer.c_str() ) != nullptr )
			{
				listFiltered.push_back( &item );
			}
		}

		const int32 filteredCount = static_cast<int32>( listFiltered.size() );
		bool		bExecute	  = EditorWidgets::updateListSelection( _selectedIndex, filteredCount, false );

		ImGui::Separator();

		editor::EditorSectionDesc resultsDesc{};
		resultsDesc._pId  = "##qlResults";
		resultsDesc._kind = editor::EditorSectionKind::Child;
		if ( EditorChrome::beginSection( resultsDesc ) )
		{
			for ( int32 itemIndex = 0; itemIndex < filteredCount; ++itemIndex )
			{
				const QuickLauncherItem* pItem	   = listFiltered[static_cast<size_t>( itemIndex )];
				const bool				 bSelected = ( itemIndex == _selectedIndex );

				ImGui::PushID( itemIndex );

				ImVec2		  cursor	= ImGui::GetCursorScreenPos();
				ImDrawList*	  pDrawList = ImGui::GetWindowDrawList();
				const float32 availW	= ImGui::GetContentRegionAvail().x;
				const float32 itemH		= 34.0f;

				if ( bSelected )
				{
					pDrawList->AddRectFilled( cursor, ImVec2( cursor.x + availW, cursor.y + itemH ),
											  IM_COL32( 40, 75, 130, 220 ), 4.0f );
					pDrawList->AddRect( cursor, ImVec2( cursor.x + availW, cursor.y + itemH ),
										IM_COL32( 80, 140, 240, 255 ), 4.0f );
				}

				const ImVec4						 catCol = QuickLauncherPopupInternal::getCategoryColor( pItem->_category );
				fixed_string<constant::kMaxBuffer32> badge;
				formatstring( badge.data(), badge.capacity(), "[%s]", pItem->_category.c_str() );

				pDrawList->AddText( ImVec2( cursor.x + 8.0f, cursor.y + 8.0f ), ImGui::ColorConvertFloat4ToU32( catCol ),
									badge.c_str() );

				pDrawList->AddText( ImVec2( cursor.x + 95.0f, cursor.y + 8.0f ), IM_COL32( 240, 240, 245, 255 ),
									pItem->_title.c_str() );

				pDrawList->AddText( ImVec2( cursor.x + 300.0f, cursor.y + 8.0f ), IM_COL32( 150, 155, 170, 200 ),
									pItem->_detail.c_str() );

				if ( ImGui::InvisibleButton( "##itemBtn", ImVec2( availW, itemH ) ) )
				{
					_selectedIndex = itemIndex;
					bExecute	   = true;
				}

				if ( bSelected )
					ImGui::SetScrollHereY( 0.5f );

				ImGui::PopID();
			}

			if ( filteredCount == 0 )
			{
				if ( _fileIndexJob.isPending() )
					EditorWidgets::drawEmptyHint( "Indexing assets..." );
				else
					EditorWidgets::drawEmptyHint( "No matching assets or game objects found." );
			}
		}
		EditorChrome::endSection();

		EditorChrome::endSearchOverlay();

		if ( bExecute && filteredCount > 0 && 0 <= _selectedIndex && _selectedIndex < filteredCount )
		{
			const QuickLauncherItem* pTarget = listFiltered[static_cast<size_t>( _selectedIndex )];
			if ( pTarget != nullptr )
				executeItem( *pTarget );
			close();
		}
	}
} // namespace sw::editor
