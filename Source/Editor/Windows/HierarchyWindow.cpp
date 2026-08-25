#include "pch.h"

#include "Editor/Windows/HierarchyWindow.h"

#include "Editor/Widgets/EditorWidgets.h"
#include "Editor/Workspace/EditorAssetDrop.h"
#include "Editor/Workspace/EditorContextMenuRegistry.h"
#include "Editor/Workspace/EditorTransaction.h"
#include "Editor/Workspace/EditorWorkspace.h"
#include "Editor/Workspace/SelectionManager.h"

#include "Engine/Game/GameState.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "RuntimeAPI/EditorService.h"
#include "RuntimeAPI/EditorUIContext.h"

#include <algorithm>
#include <imgui.h>
namespace sw
{

	namespace
	{
		constexpr const utf8* kHierarchyGoPayload = "SW_HIERARCHY_GO";

		bool typeMatchesFilter( GameObject* pObj, string_view typeFilter )
		{
			if ( pObj == nullptr || typeFilter.empty() )
				return false;

			for ( Component* pComp : pObj->getAllComponents() )
			{
				if ( pComp == nullptr )
					continue;

				if ( StringUtil::strnicmp( pComp->getComponentName().c_str(), typeFilter.data(),
										   static_cast<uint32>( typeFilter.size() ) ) == 0 )
					return true;

				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr && StringUtil::strnicmp( pTypeInfo->_name.c_str(), typeFilter.data(),
																   static_cast<uint32>( typeFilter.size() ) ) == 0 )
					return true;
			}
			return false;
		}

		bool nameMatchesFilter( GameObject* pObj, const utf8* pFilter )
		{
			if ( pFilter == nullptr || pFilter[0] == '\0' )
				return true;
			if ( pObj == nullptr )
				return false;

			// 1) Type syntax "t:ComponentName"
			if ( pFilter[0] == 't' && pFilter[1] == ':' )
			{
				return typeMatchesFilter( pObj, string_view{ pFilter + 2 } );
			}

			// 2) Tag syntax "tag:TagName"
			if ( StringUtil::strnicmp( pFilter, "tag:", 4 ) == 0 )
			{
				const string_view tagFilter{ pFilter + 4 };
				return pObj->hasTag( TagID{ hashed_string( tagFilter ).getHash(), nullptr } );
			}

			// 3) General name matching
			const string_view name = pObj->getName().c_str();
			const size_t	  flen = StringUtil::strlen( pFilter );
			if ( flen == 0 )
				return true;
			if ( name.size() < flen )
				return false;
			for ( size_t matchIndex = 0; matchIndex <= name.size() - flen; ++matchIndex )
			{
				if ( StringUtil::strnicmp( name.data() + matchIndex, pFilter, static_cast<uint32>( flen ) ) == 0 )
					return true;
			}
			return false;
		}

		bool subtreeMatchesFilter( GameObject* pObj, const utf8* pFilter )
		{
			if ( pObj == nullptr )
				return false;
			if ( nameMatchesFilter( pObj, pFilter ) )
				return true;
			for ( GameObject* pChild : pObj->getChildren() )
			{
				if ( subtreeMatchesFilter( pChild, pFilter ) )
					return true;
			}
			return false;
		}

		GameObject* createGameObjectWithRoot( GameObjectManager* pManager, GameObject* pParent )
		{
			if ( pManager == nullptr )
				return nullptr;

			GameObject* pCreated = pManager->createGameObject( hashed_string( "GameObject" ) );
			if ( pCreated == nullptr )
				return nullptr;

			pCreated->addComponent<SceneComponent>();
			if ( pParent != nullptr )
				pCreated->attachToParent( pParent );

			EditorTransaction::recordCreation( GameObjectPtr{ pCreated }, "Create GameObject" );
			EditorWorkspace::selectGameObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
			return pCreated;
		}

		bool wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent )
		{
			if ( pChild == nullptr || pNewParent == nullptr || pChild == pNewParent )
				return true;

			GameObject* pAncestor = pNewParent;
			while ( pAncestor != nullptr )
			{
				if ( pAncestor == pChild )
					return true;
				pAncestor = pAncestor->getParent();
			}
			return false;
		}

		void handleHierarchyReparentDrop( GameObject* pTargetParent, const ImGuiPayload* pPayload,
										  GameObjectManager* pManager )
		{
			if ( pPayload == nullptr || pManager == nullptr || pTargetParent == nullptr )
				return;
			if ( pPayload->DataSize != static_cast<int32>( sizeof( uint64 ) ) )
				return;

			const uint64	  draggedId = *static_cast<const uint64*>( pPayload->Data );
			GameObject* const pDragged	= pManager->findGameObjectById( draggedId );
			if ( pDragged == nullptr || pDragged == pTargetParent )
				return;
			if ( wouldCreateParentCycle( pDragged, pTargetParent ) )
				return;

			const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pDragged } );
			if ( pDragged->attachToParent( pTargetParent ) )
			{
				const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pDragged } );
				EditorTransaction::recordModify( GameObjectPtr{ pDragged }, beforeXml, afterXml, "Reparent GameObject" );
				EditorWorkspace::selectGameObject( GameObjectPtr{ pDragged }, SelectionMode::Replace );
			}
		}

		void drawGameObjectDragDrop( GameObject* pObj, GameObjectManager* pManager )
		{
			if ( pObj == nullptr || pManager == nullptr )
				return;

			if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
			{
				const uint64 id = pObj->getObjectId();
				ImGui::SetDragDropPayload( kHierarchyGoPayload, &id, sizeof( id ) );
				ImGui::TextUnformatted( pObj->getName().c_str() );
				ImGui::EndDragDropSource();
			}

			if ( ImGui::BeginDragDropTarget() )
			{
				const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( kHierarchyGoPayload );
				if ( pPayload != nullptr )
					handleHierarchyReparentDrop( pObj, pPayload, pManager );

				const ImGuiPayload* pAssetPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" );
				if ( pAssetPayload != nullptr )
				{
					const utf8* pPath = static_cast<const utf8*>( pAssetPayload->Data );
					if ( pPath != nullptr )
					{
						GameObject* pSpawned = editor::spawnPrefabFromAssetPath( pManager, pPath, pObj );
						if ( pSpawned != nullptr )
						{
							EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, "Spawn Prefab" );
							EditorWorkspace::selectGameObject( GameObjectPtr{ pSpawned }, SelectionMode::Replace );
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		void drawComponentContextMenu( GameObject* pObj, Component* pComp, GameObjectManager* pManager )
		{
			if ( ImGui::BeginPopupContextItem( "CompCtx" ) == false )
				return;

			if ( ImGui::MenuItem( "Select Owner GameObject" ) )
				EditorWorkspace::selectGameObject( GameObjectPtr{ pObj } );

			if ( ImGui::MenuItem( "Remove Component" ) && pComp != nullptr && pManager != nullptr )
			{
				if ( EditorWorkspace::selectedObjectId() == pObj->getObjectId() &&
					 EditorWorkspace::selectedComponentId() == pComp->getComponentId() )
				{
					EditorWorkspace::selectedComponentId() = 0;
					EditorWorkspace::selectedComponentKey().clear();
				}
				pManager->destroyComponent( pComp );
			}

			ImGui::EndPopup();
		}

		void drawAddComponentMenu( GameObject* pObj )
		{
			if ( ImGui::BeginMenu( "Add Component" ) == false )
				return;

			vector<hashed_string> listTypes;
			if ( pObj != nullptr && pObj->getManager() != nullptr )
				listTypes = pObj->getManager()->getRegisteredComponentTypeNames();
			if ( listTypes.empty() )
				ImGui::TextDisabled( "No registered component types." );
			else
			{
				for ( const hashed_string& typeName : listTypes )
				{
					if ( ImGui::MenuItem( typeName.c_str() ) )
					{
						if ( pObj->addComponentByName( typeName ) == nullptr )
							ImGui::OpenPopup( "AddCompFailed" );
					}
				}
			}
			ImGui::EndMenu();
		}

		void drawGameObjectContextMenu( GameObject* pObj, GameObjectManager* pManager )
		{
			if ( ImGui::BeginPopupContextItem( "GOCtx" ) == false )
				return;

			if ( ImGui::MenuItem( "Create GameObject" ) )
				createGameObjectWithRoot( pManager, nullptr );

			if ( ImGui::MenuItem( "Create Child GameObject" ) )
				createGameObjectWithRoot( pManager, pObj );

			drawAddComponentMenu( pObj );

			ImGui::Separator();

			if ( pObj->getParent() != nullptr )
			{
				if ( ImGui::MenuItem( "Unparent" ) )
				{
					const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
					pObj->detachFromParent();
					const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
					EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml, "Unparent GameObject" );
				}
			}

			GameObject* pSelected = pManager->findGameObjectById( EditorWorkspace::selectedObjectId() );
			const bool	bCanParentToSelected =
				pSelected != nullptr && pSelected != pObj && EditorWorkspace::selectedComponentId() == 0;
			if ( bCanParentToSelected )
			{
				bool		bWouldCycle{ false };
				GameObject* pAncestor = pSelected;
				while ( pAncestor != nullptr )
				{
					if ( pAncestor == pObj )
					{
						bWouldCycle = true;
						break;
					}
					pAncestor = pAncestor->getParent();
				}

				if ( bWouldCycle == false )
				{
					if ( ImGui::MenuItem( "Parent to Selected" ) )
					{
						const string beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
						pObj->attachToParent( pSelected );
						const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pObj } );
						EditorTransaction::recordModify( GameObjectPtr{ pObj }, beforeXml, afterXml,
														 "Parent to Selected" );
					}
				}
				else
				{
					ImGui::BeginDisabled();
					ImGui::MenuItem( "Parent to Selected" );
					ImGui::EndDisabled();
				}
			}

			// 동적 확장 메뉴
			EditorContextMenuRegistry::drawContextMenu( ContextMenuLocation::Hierarchy );

			ImGui::Separator();
			if ( ImGui::MenuItem( "Destroy GameObject" ) )
			{
				EditorTransaction::recordDestruction( GameObjectPtr{ pObj }, "Destroy GameObject" );
				if ( SelectionManager::hasObject( GameObjectPtr{ pObj } ) )
					SelectionManager::selectObject( GameObjectPtr{ pObj }, SelectionMode::Remove );
				pManager->destroyObject( pObj );
			}

			ImGui::EndPopup();
		}

		void drawSceneComponentNode( GameObject* pObj, SceneComponent* pSceneComp, GameObjectManager* pManager )
		{
			if ( pObj == nullptr || pSceneComp == nullptr )
				return;

			ImGui::PushID( static_cast<int32>( pSceneComp->getComponentId() ) );

			const bool bSelected = ( EditorWorkspace::selectedObjectId() == pObj->getObjectId() &&
									 EditorWorkspace::selectedComponentId() == pSceneComp->getComponentId() );

			const utf8* pCompName = pSceneComp->getComponentName().empty() == false
									  ? pSceneComp->getComponentName().c_str()
									  : "SceneComponent";

			utf8 arrLabel[256];
			formatstring( arrLabel, sizeof( arrLabel ), "%###sc%#", pCompName, pSceneComp->getComponentId() );

			bool						   hasChildOnOwner{ false };
			const vector<SceneComponent*>& listChildren = pSceneComp->getChildren();
			for ( SceneComponent* pChild : listChildren )
			{
				if ( pChild != nullptr && pChild->getOwner() == pObj )
				{
					hasChildOnOwner = true;
					break;
				}
			}

			const ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
				( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) | ( hasChildOnOwner ? 0 : ImGuiTreeNodeFlags_Leaf );

			const bool bOpen = ImGui::TreeNodeEx( arrLabel, flags );
			if ( ImGui::IsItemClicked() )
				EditorWorkspace::selectComponent( GameObjectPtr{ pObj }, ComponentPtr{ pSceneComp } );
			drawComponentContextMenu( pObj, pSceneComp, pManager );

			if ( bOpen )
			{
				for ( SceneComponent* pChild : listChildren )
				{
					if ( pChild != nullptr && pChild->getOwner() == pObj )
						drawSceneComponentNode( pObj, pChild, pManager );
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		void drawGameObjectNode( GameObject* pObj, GameObjectManager* pManager, const utf8* pFilter )
		{
			if ( pObj == nullptr || pManager == nullptr )
				return;

			if ( pFilter != nullptr && pFilter[0] != '\0' )
			{
				if ( subtreeMatchesFilter( pObj, pFilter ) == false )
					return;
			}

			const uint64  objectId = pObj->getObjectId();
			GameObjectPtr ptrObj{ pObj };
			const bool	  bSelected = SelectionManager::hasObject( ptrObj );

			ImGui::PushID( static_cast<int32>( objectId ) );

			// 1) Visibility Toggle Icon (Eye)
			bool bActive = pObj->isActiveInHierarchy();
			if ( ImGui::Button( bActive ? "[V]" : "[.]", ImVec2{ 24.0f, 0.0f } ) )
			{
				pObj->setActive( !bActive );
			}
			ImGui::SameLine();

			utf8 arrLabel[256];
			formatstring( arrLabel, sizeof( arrLabel ), "%###go%#", pObj->getName().c_str(), objectId );

			const bool bHasChildGos	  = pObj->getChildren().empty() == false;
			const bool bHasComponents = pObj->getComponentCount() > 0;
			const bool bLeaf		  = ( bHasChildGos == false && bHasComponents == false );

			const bool bOpen = ImGui::TreeNodeEx(
				arrLabel,
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
					( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) | ( bLeaf ? ImGuiTreeNodeFlags_Leaf : 0 ) );

			if ( ImGui::IsItemClicked() )
			{
				ImGuiIO&	  io   = ImGui::GetIO();
				SelectionMode mode = SelectionMode::Replace;
				if ( io.KeyCtrl )
					mode = SelectionMode::Toggle;
				else if ( io.KeyShift )
					mode = SelectionMode::Add;

				EditorWorkspace::selectGameObject( ptrObj, mode );
			}

			drawGameObjectContextMenu( pObj, pManager );
			drawGameObjectDragDrop( pObj, pManager );

			if ( bOpen )
			{
				for ( GameObject* pChild : pObj->getChildren() )
				{
					drawGameObjectNode( pChild, pManager, pFilter );
				}

				const vector<Component*>& listComponents = pObj->getAllComponents();
				for ( Component* pComp : listComponents )
				{
					if ( pComp == nullptr )
						continue;

					SceneComponent* pSceneComp = pComp->asSceneComponent();
					if ( pSceneComp != nullptr )
					{
						const SceneComponent* pParent		= pSceneComp->getParent();
						const bool			  bRootOnThisGo = pParent == nullptr || pParent->getOwner() != pObj;
						if ( bRootOnThisGo )
							drawSceneComponentNode( pObj, pSceneComp, pManager );
						continue;
					}

					ImGui::PushID( static_cast<int32>( pComp->getComponentId() ) );

					const bool bCompSelected = ( EditorWorkspace::selectedObjectId() == pObj->getObjectId() &&
												 EditorWorkspace::selectedComponentId() == pComp->getComponentId() );

					const utf8* pCompName = pComp->getComponentName().empty() == false
											  ? pComp->getComponentName().c_str()
											  : "Component";

					utf8 arrCompLabel[256];
					formatstring( arrCompLabel, sizeof( arrCompLabel ), "%###c%#", pCompName,
								  pComp->getComponentId() );

					if ( ImGui::Selectable( arrCompLabel, bCompSelected ) )
						EditorWorkspace::selectComponent( ptrObj, ComponentPtr{ pComp } );
					drawComponentContextMenu( pObj, pComp, pManager );

					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

	} // namespace

	void HierarchyWindow::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			ImGui::TextDisabled( "No active scene." );
			ImGui::End();
			return;
		}

		GameObjectManager*		   pManager	   = pScene->getObjectManager();
		const vector<GameObject*>& listObjects = pManager->getAllGameObjects();

		// 상단 툴바: 생성 버튼 + 검색창
		if ( ImGui::Button( "+ Create" ) )
			createGameObjectWithRoot( pManager, nullptr );

		ImGui::SameLine();
		ImGui::InputTextWithHint( "##HierarchyFilter", "Search (t:Mesh, tag:Player)...", _arrFilterBuffer,
								  sizeof( _arrFilterBuffer ) );

		ImGui::Separator();

		if ( ImGui::BeginChild( "##HierarchyTree", ImVec2{ 0, 0 }, false, ImGuiWindowFlags_None ) )
		{
			for ( GameObject* pObj : listObjects )
			{
				if ( pObj != nullptr && pObj->getParent() == nullptr )
					drawGameObjectNode( pObj, pManager, _arrFilterBuffer );
			}

			// 빈 공간 우클릭 메뉴
			if ( ImGui::BeginPopupContextWindow( "HierarchyEmptyCtx",
												 ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
			{
				if ( ImGui::MenuItem( "Create Empty GameObject" ) )
					createGameObjectWithRoot( pManager, nullptr );

				EditorContextMenuRegistry::drawContextMenu( ContextMenuLocation::Hierarchy );
				ImGui::EndPopup();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
} // namespace sw
