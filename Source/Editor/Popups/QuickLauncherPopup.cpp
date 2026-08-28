#include "pch.h"

#include "Editor/Popups/QuickLauncherPopup.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
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
		ImVec4 getCategoryColor( string_view cat )
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
	} // namespace

	QuickLauncherPopup::QuickLauncherPopup()
		: _listAllItem{}
		, _arrSearchBuffer{}
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
		_bJustOpened		= true;
		_selectedIndex		= 0;
		_arrSearchBuffer[0] = '\0';
		rebuildIndex();
	}

	void QuickLauncherPopup::rebuildIndex()
	{
		_listAllItem.clear();

		// 1) 씬 내 게임 오브젝트 인덱싱
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

					const uint64	  objId	  = pObj->getObjectId();
					const string	  objName = string{ pObj->getName().c_str() };
					QuickLauncherItem item{};
					item._category		 = "GameObject";
					item._title			 = objName;
					item._detail		 = "Scene GameObject (ID: " + std::to_string( objId ) + ")";
					item._targetObjectId = objId;
					item._action		 = [objId]()
					{
						SceneManager* pMgr = editor::getService<SceneManager>();
						if ( pMgr != nullptr && pMgr->getActiveScene() != nullptr &&
							 pMgr->getActiveScene()->getObjectManager() != nullptr )
						{
							GameObject* pFound = pMgr->getActiveScene()->getObjectManager()->findGameObjectById( objId );
							if ( pFound != nullptr )
							{
								EditorContext::get()->getWorkspace().selectGameObject( GameObjectPtr{ pFound },
																					   SelectionMode::Replace );
							}
						}
					};
					_listAllItem.push_back( std::move( item ) );
				}
			}
		}

		// 2) Resource 폴더 내 모든 에셋 인덱싱 (Scenes, Prefabs, Textures, Shaders, Data XMLs)
		const string   resourceFolder = FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource" );
		vector<string> listAllFiles;
		FileUtil::collectFiles( resourceFolder, "", listAllFiles, true, false );

		for ( const string& file : listAllFiles )
		{
			const string ext	  = FileUtil::getExtension( file );
			const string filename = FileUtil::getFileNamePart( file );
			string		 relPath;
			FileUtil::makePathRelative( FileUtil::getCurrentPath(), file, relPath );
			relPath = FileUtil::normalizeSeparators( relPath );

			QuickLauncherItem item{};
			item._path = relPath;

			if ( ext == ".scene" || ( ext == ".xml" && filename.find( ".scene" ) != string::npos ) )
			{
				item._category = "Scene";
				item._title	   = filename;
				item._detail   = relPath;
				item._action   = [relPath]()
				{ EditorContext::get()->getWorkspace().requestLoadScene( relPath ); };
				_listAllItem.push_back( std::move( item ) );
			}
			else if ( ext == ".prefab" || ext == ".pfb" )
			{
				item._category = "Prefab";
				item._title	   = filename;
				item._detail   = relPath;
				item._action   = [relPath]()
				{ EditorContext::get()->getWorkspace().setFocusedAssetPath( relPath.c_str() ); };
				_listAllItem.push_back( std::move( item ) );
			}
			else if ( ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".tga" || ext == ".bmp" )
			{
				item._category = "Texture";
				item._title	   = filename;
				item._detail   = relPath;
				item._action   = [relPath]()
				{ EditorContext::get()->getWorkspace().setFocusedAssetPath( relPath.c_str() ); };
				_listAllItem.push_back( std::move( item ) );
			}
			else if ( ext == ".hlsl" || ext == ".glsl" || ext == ".spv" )
			{
				item._category = "Shader";
				item._title	   = filename;
				item._detail   = relPath;
				item._action   = [relPath]()
				{ EditorContext::get()->getWorkspace().setFocusedAssetPath( relPath.c_str() ); };
				_listAllItem.push_back( std::move( item ) );
			}
			else if ( ext == ".xml" || ext == ".json" )
			{
				item._category = "Data";
				item._title	   = filename;
				item._detail   = relPath;
				item._action   = [relPath]()
				{ EditorContext::get()->getWorkspace().setFocusedAssetPath( relPath.c_str() ); };
				_listAllItem.push_back( std::move( item ) );
			}
		}
	}

	void QuickLauncherPopup::drawContent()
	{
		editor::EditorSearchOverlayDesc overlayDesc{};
		overlayDesc._pId		  = "##QuickLauncherOverlay";
		overlayDesc._pOpen		  = &_bOpen;
		overlayDesc._size		  = float2{ 620.0f, 400.0f };
		overlayDesc._borderColor  = float4{ 0.25f, 0.55f, 0.85f, 1.0f };
		overlayDesc._pFocusOnOpen = &_bJustOpened;

		if ( editor::beginSearchOverlay( overlayDesc ) == false )
		{
			editor::endSearchOverlay();
			close();
			return;
		}

		editor::drawSearchField( "##qlSearch", _arrSearchBuffer, sizeof( _arrSearchBuffer ),
								 "Type to search assets, scenes, game objects (ESC to cancel)...", -1.0f, true );

		const string filter = StringUtil::toLower( _arrSearchBuffer );

		vector<const QuickLauncherItem*> listFiltered;
		listFiltered.reserve( _listAllItem.size() );

		for ( const QuickLauncherItem& item : _listAllItem )
		{
			if ( filter.empty() )
			{
				listFiltered.push_back( &item );
				continue;
			}

			const string lowerTitle	 = StringUtil::toLower( item._title.c_str() );
			const string lowerDetail = StringUtil::toLower( item._detail.c_str() );
			const string lowerCat	 = StringUtil::toLower( item._category.c_str() );

			if ( lowerTitle.find( filter ) != string::npos || lowerDetail.find( filter ) != string::npos ||
				 lowerCat.find( filter ) != string::npos )
			{
				listFiltered.push_back( &item );
			}
		}

		const int32 filteredCount = static_cast<int32>( listFiltered.size() );
		bool		bExecute	  = editor::updateListSelection( _selectedIndex, filteredCount, false );

		ImGui::Separator();

		editor::EditorSectionDesc resultsDesc{};
		resultsDesc._pId  = "##qlResults";
		resultsDesc._kind = editor::EditorSectionKind::Child;
		if ( editor::beginSection( resultsDesc ) )
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

				const ImVec4 catCol = getCategoryColor( pItem->_category );
				utf8		 arrBadge[32];
				formatstring( arrBadge, sizeof( arrBadge ), "[%s]", pItem->_category.c_str() );

				pDrawList->AddText( ImVec2( cursor.x + 8.0f, cursor.y + 8.0f ), ImGui::ColorConvertFloat4ToU32( catCol ),
									arrBadge );

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
				editor::drawEmptyHint( "No matching assets or game objects found." );
		}
		editor::endSection();

		editor::endSearchOverlay();

		if ( bExecute && filteredCount > 0 && 0 <= _selectedIndex && _selectedIndex < filteredCount )
		{
			const QuickLauncherItem* pTarget = listFiltered[static_cast<size_t>( _selectedIndex )];
			if ( pTarget != nullptr && pTarget->_action.isBound() )
				pTarget->_action();
			close();
		}
	}
} // namespace sw::editor
