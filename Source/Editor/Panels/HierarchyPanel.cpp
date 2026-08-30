#include "pch.h"

#include "Editor/Panels/HierarchyPanel.h"

#include "Core/Common/StdHeaders.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorActionMenuManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct HierarchyPanelInternal
		{
			static constexpr const utf8* kHierarchyGoPayload = "SW_HIERARCHY_GO";

			static bool typeMatchesFilter( GameObject* pObj, string_view typeFilter )
			{
				if ( pObj == nullptr || typeFilter.empty() )
					return false;

				for ( Component* pComp : pObj->getAllComponents() )
				{
					if ( pComp == nullptr )
						continue;

					if ( StringUtil::startsWith( pComp->getComponentName().view(), typeFilter, true ) )
						return true;

					const TypeInfo* pTypeInfo = pComp->getTypeInfo();
					if ( pTypeInfo != nullptr && StringUtil::startsWith( pTypeInfo->_name.view(), typeFilter, true ) )
						return true;
				}
				return false;
			}

			static bool nameMatchesFilter( GameObject* pObj, const utf8* pFilter )
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
				if ( StringUtil::startsWith( pFilter, "tag:", true ) )
				{
					const string_view tagFilter{ pFilter + 4 };
					return pObj->hasTag( TagID{ hashed_string( tagFilter ).getHash(), nullptr } );
				}

				// 3) General name matching
				return StringUtil::stristr( pObj->getName().c_str(), pFilter ) != nullptr;
			}

			static bool subtreeMatchesFilter( GameObject* pObj, const utf8* pFilter )
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

			static void handleHierarchyReparentDrop( GameObject* pTargetParent, const ImGuiPayload* pPayload,
													 GameObjectManager* pManager )
			{
				if ( pPayload == nullptr || pManager == nullptr || pTargetParent == nullptr )
					return;
				if ( pPayload->DataSize != static_cast<int32>( sizeof( uint64 ) ) )
					return;

				const uint64	  draggedId = *static_cast<const uint64*>( pPayload->Data );
				GameObject* const pDragged	= pManager->findGameObjectById( draggedId );
				EditorSceneCommands::reparent( pDragged, pTargetParent );
			}

			static void drawGameObjectDragDrop( GameObject* pObj, GameObjectManager* pManager )
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

					string droppedAssetPath;
					if ( EditorWidgets::tryAcceptAssetPayload( droppedAssetPath ) )
						EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), pObj );
					ImGui::EndDragDropTarget();
				}
			}

			static void drawComponentContextMenu( GameObject* pObj, Component* pComp, GameObjectManager* pManager )
			{
				if ( ImGui::BeginPopupContextItem( "CompCtx" ) == false )
					return;

				if ( ImGui::MenuItem( "Select Owner GameObject" ) )
					EditorContext::get()->getWorkspace().selectGameObject( GameObjectPtr{ pObj } );

				const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
				if ( bEditsAllowed == false )
					ImGui::BeginDisabled();
				if ( ImGui::MenuItem( "Remove Component" ) && pComp != nullptr && pManager != nullptr )
					EditorSceneCommands::destroyComponent( pManager, pObj, pComp );
				if ( bEditsAllowed == false )
					ImGui::EndDisabled();

				ImGui::EndPopup();
			}

			static void drawAddComponentMenu( GameObject* pObj )
			{
				if ( ImGui::BeginMenu( "Add Component" ) == false )
					return;

				vector<hashed_string> listTypes;
				if ( pObj != nullptr && pObj->getManager() != nullptr )
					listTypes = pObj->getManager()->getRegisteredComponentTypeNames();
				if ( listTypes.empty() )
				{
					ImGui::TextDisabled( "No registered component types." );
					ImGui::EndMenu();
					return;
				}

				static fixed_string<constant::kMaxBuffer64> s_searchBuf;
				ImGui::SetNextItemWidth( 180.0f );
				ImGui::InputTextWithHint( "##compSearch", "Search...", s_searchBuf.data(),
										  s_searchBuf.capacity() );
				const bool bHasFilter = ( s_searchBuf.empty() == false );

				auto* pRegistry = editor::getService<TypeRegistry>();

				auto drawItem = [&]( const hashed_string& typeName, const TypeInfo* pTypeInfo )
				{
					const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
					if ( ImGui::MenuItem( pDisplayName ) )
					{
						if ( pObj->getManager()->addComponentByName( pObj, typeName ) == nullptr )
							ImGui::OpenPopup( "AddCompFailed" );
					}
					if ( pTypeInfo != nullptr && pTypeInfo->getTooltip().empty() == false && ImGui::IsItemHovered() )
						ImGui::SetTooltip( "%s", pTypeInfo->getTooltip().c_str() );
				};

				if ( bHasFilter )
				{
					ImGui::Separator();
					uint32 matchCount{ 0 };
					for ( const hashed_string& typeName : listTypes )
					{
						const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
						if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
							continue;

						const utf8* pDisplayName = ( pTypeInfo != nullptr ) ? pTypeInfo->getDisplayName() : typeName.c_str();
						if ( StringUtil::stristr( pDisplayName, s_searchBuf.c_str() ) != nullptr ||
							 StringUtil::stristr( typeName.c_str(), s_searchBuf.c_str() ) != nullptr )
						{
							drawItem( typeName, pTypeInfo );
							++matchCount;
						}
					}
					if ( matchCount == 0 )
						ImGui::TextDisabled( "No matching components." );
				}
				else
				{
					map<string, vector<pair<hashed_string, const TypeInfo*>>> mapCategorized;
					for ( const hashed_string& typeName : listTypes )
					{
						const TypeInfo* pTypeInfo = ( pRegistry != nullptr ) ? pRegistry->findType( typeName ) : nullptr;
						if ( pTypeInfo != nullptr && pTypeInfo->isHiddenInMenu() )
							continue;

						string category = ( pTypeInfo != nullptr && pTypeInfo->getCategory().empty() == false )
											? pTypeInfo->getCategory()
											: "General";
						mapCategorized[category].emplace_back( typeName, pTypeInfo );
					}

					for ( const auto& [category, items] : mapCategorized )
					{
						if ( category == "General" )
						{
							for ( const auto& [typeName, pTypeInfo] : items )
								drawItem( typeName, pTypeInfo );
						}
						else
						{
							if ( ImGui::BeginMenu( category.c_str() ) )
							{
								for ( const auto& [typeName, pTypeInfo] : items )
									drawItem( typeName, pTypeInfo );
								ImGui::EndMenu();
							}
						}
					}
				}
				ImGui::EndMenu();
			}

			static void drawGameObjectContextMenu( GameObject* pObj, GameObjectManager* pManager )
			{
				if ( ImGui::BeginPopupContextItem( "GOCtx" ) == false )
					return;

				const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
				if ( bEditsAllowed == false )
					ImGui::BeginDisabled();

				if ( ImGui::MenuItem( "Create GameObject" ) )
					EditorSceneCommands::create( pManager, nullptr );

				if ( ImGui::MenuItem( "Create Child GameObject" ) )
					EditorSceneCommands::create( pManager, pObj );

				if ( ImGui::MenuItem( "Duplicate GameObject", "Ctrl+D" ) )
					EditorSceneCommands::duplicate( pManager, pObj );

				drawAddComponentMenu( pObj );

				ImGui::Separator();

				if ( pObj->getParent() != nullptr )
				{
					if ( ImGui::MenuItem( "Unparent" ) )
						EditorSceneCommands::unparent( pObj );
				}

				GameObject* pSelected = pManager->findGameObjectById( EditorContext::get()->getWorkspace().getSelectedObjectId() );
				const bool	bCanParentToSelected =
					pSelected != nullptr && pSelected != pObj && EditorContext::get()->getWorkspace().getSelectedComponentId() == 0;
				if ( bCanParentToSelected )
				{
					if ( EditorSceneCommands::wouldCreateParentCycle( pObj, pSelected ) == false )
					{
						if ( ImGui::MenuItem( "Parent to Selected" ) )
							EditorSceneCommands::reparent( pObj, pSelected, "Parent to Selected" );
					}
					else
					{
						ImGui::BeginDisabled();
						ImGui::MenuItem( "Parent to Selected" );
						ImGui::EndDisabled();
					}
				}

				// 동적 확장 메뉴
				EditorContext::get()->getActionMenuManager().drawActionMenu( ActionMenuLocation::Hierarchy );

				ImGui::Separator();
				if ( ImGui::MenuItem( "Destroy GameObject", "Delete" ) )
					EditorSceneCommands::destroy( pManager, pObj );

				if ( bEditsAllowed == false )
					ImGui::EndDisabled();
				ImGui::EndPopup();
			}

			static void drawSceneComponentNode( GameObject* pObj, SceneComponent* pSceneComp, GameObjectManager* pManager )
			{
				if ( pObj == nullptr || pSceneComp == nullptr )
					return;

				ImGui::PushID( static_cast<int32>( pSceneComp->getComponentId() ) );

				EditorWorkspace& ws		   = EditorContext::get()->getWorkspace();
				const bool		 bSelected = ( ws.getSelectedObjectId() == pObj->getObjectId() &&
											   ws.getSelectedComponentId() == pSceneComp->getComponentId() );

				const utf8* pCompName = pSceneComp->getComponentName().empty() == false
										  ? pSceneComp->getComponentName().c_str()
										  : "SceneComponent";

				fixed_string<constant::kMaxBuffer256> arrLabel;
				formatstring( arrLabel.data(), arrLabel.capacity(), "%###sc%#", pCompName, pSceneComp->getComponentId() );

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

				const bool bOpen = ImGui::TreeNodeEx( arrLabel.c_str(), flags );
				if ( ImGui::IsItemClicked() )
					ws.selectComponent( GameObjectPtr{ pObj }, ComponentPtr{ pSceneComp } );
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

			static void drawGameObjectNode( GameObject* pObj, GameObjectManager* pManager, const utf8* pFilter,
											uint64& renamingObjectId, fixed_string<constant::kMaxBuffer256>& renameBuffer,
											bool& bFocusRenameInput )
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
				const bool	  bSelected = EditorContext::get()->getSelectionManager().hasObject( ptrObj );

				ImGui::PushID( static_cast<int32>( objectId ) );

				// 1) Visibility Toggle Icon (Eye)
				bool bActive = pObj->isActiveInHierarchy();
				if ( ImGui::Button( bActive ? "[V]" : "[.]", ImVec2{ 24.0f, 0.0f } ) )
				{
					pObj->setActive( bActive == false );
				}
				ImGui::SameLine();

				string badgeStr;
				for ( const Component* pComp : pObj->getAllComponents() )
				{
					if ( pComp == nullptr )
						continue;
					const TypeInfo* pT = pComp->getTypeInfo();
					if ( pT == nullptr )
						continue;
					const hashed_string& typeName = pT->_name;
					if ( typeName == hashed_string( "CameraComponent" ) )
						badgeStr += " [Cam]";
					else if ( typeName == hashed_string( "MeshComponent" ) )
						badgeStr += " [Mesh]";
					else if ( typeName == hashed_string( "SpriteComponent" ) )
						badgeStr += " [Sprite]";
					else if ( typeName == hashed_string( "SpriteAnimatorComponent" ) )
						badgeStr += " [Anim]";
					else if ( typeName == hashed_string( "BoxCollider2DComponent" ) )
						badgeStr += " [Col]";
					else if ( typeName == hashed_string( "UnitStatsComponent" ) )
						badgeStr += " [Stats]";
					else if ( typeName == hashed_string( "HPBarBaseComponent" ) )
						badgeStr += " [UI]";
				}

				fixed_string<constant::kMaxBuffer256> arrLabel;
				if ( badgeStr.empty() == false )
					formatstring( arrLabel.data(), arrLabel.capacity(), "%# %#%###go%#", pObj->getName().c_str(), badgeStr.c_str(), objectId );
				else
					formatstring( arrLabel.data(), arrLabel.capacity(), "%###go%#", pObj->getName().c_str(), objectId );

				const bool bHasChildGos	  = pObj->getChildren().empty() == false;
				const bool bHasComponents = pObj->getComponentCount() > 0;
				const bool bLeaf		  = ( bHasChildGos == false && bHasComponents == false );

				const bool bOpen = ImGui::TreeNodeEx(
					arrLabel.c_str(),
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

					EditorContext::get()->getWorkspace().selectGameObject( ptrObj, mode );
				}

				// Inline Rename Input
				if ( renamingObjectId == objectId && EditorUtil::areSceneEditsAllowed() )
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth( 160.0f );
					if ( bFocusRenameInput )
					{
						ImGui::SetKeyboardFocusHere();
						bFocusRenameInput = false;
					}
					if ( ImGui::InputText( "##InlineRename", renameBuffer.data(), renameBuffer.capacity(),
										   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
					{
						EditorSceneCommands::rename( pObj, renameBuffer.c_str() );
						renamingObjectId = 0;
					}
					if ( ImGui::IsItemDeactivated() && ImGui::IsKeyPressed( ImGuiKey_Escape ) == false )
					{
						EditorSceneCommands::rename( pObj, renameBuffer.c_str() );
						renamingObjectId = 0;
					}
					if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
					{
						renamingObjectId = 0;
					}
				}

				drawGameObjectContextMenu( pObj, pManager );
				drawGameObjectDragDrop( pObj, pManager );

				if ( bOpen )
				{
					for ( GameObject* pChild : pObj->getChildren() )
					{
						drawGameObjectNode( pChild, pManager, pFilter, renamingObjectId, renameBuffer,
											bFocusRenameInput );
					}

					const vector<Component*>& listComponents = pObj->getAllComponents();
					for ( Component* pComp : listComponents )
					{
						if ( pComp == nullptr )
							continue;

						SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
						if ( pSceneComp != nullptr )
						{
							const SceneComponent* pParent		= pSceneComp->getParent();
							const bool			  bRootOnThisGo = pParent == nullptr || pParent->getOwner() != pObj;
							if ( bRootOnThisGo )
								drawSceneComponentNode( pObj, pSceneComp, pManager );
							continue;
						}

						ImGui::PushID( static_cast<int32>( pComp->getComponentId() ) );

						EditorWorkspace& ws			   = EditorContext::get()->getWorkspace();
						const bool		 bCompSelected = ( ws.getSelectedObjectId() == pObj->getObjectId() &&
														   ws.getSelectedComponentId() == pComp->getComponentId() );

						const utf8* pCompName = pComp->getComponentName().empty() == false
												  ? pComp->getComponentName().c_str()
												  : "Component";

						fixed_string<constant::kMaxBuffer256> arrCompLabel;
						formatstring( arrCompLabel.data(), arrCompLabel.capacity(), "%###c%#", pCompName, pComp->getComponentId() );

						if ( ImGui::Selectable( arrCompLabel.c_str(), bCompSelected ) )
							ws.selectComponent( ptrObj, ComponentPtr{ pComp } );
						drawComponentContextMenu( pObj, pComp, pManager );

						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	HierarchyPanel::HierarchyPanel()
		: _renamingObjectId{ 0 }
		, _filterBuffer{}
		, _renameBuffer{}
		, _bFocusRenameInput{ false }
	{
	}

	void HierarchyPanel::drawContent()
	{
		Scene* pScene = editor::getService<SceneManager>()->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
		{
			EditorWidgets::drawEmptyHint( "No active scene." );
			return;
		}

		GameObjectManager*		   pManager	   = pScene->getObjectManager();
		const vector<GameObject*>& listObjects = pManager->getAllGameObjects();

		// 상단 툴바: 생성 버튼 + 검색창
		if ( EditorChrome::beginToolbar( "##HierarchyToolbar" ) )
		{
			const bool bEditsAllowed = EditorUtil::areSceneEditsAllowed();
			if ( bEditsAllowed == false )
			{
				EditorWidgets::drawChip( "Play Mode", editor::style::kWarn );
				ImGui::SameLine();
			}
			if ( bEditsAllowed == false )
				ImGui::BeginDisabled();
			if ( ImGui::Button( "+ Create" ) )
				EditorSceneCommands::create( pManager, nullptr );
			if ( bEditsAllowed == false )
				ImGui::EndDisabled();

			ImGui::SameLine();
			EditorWidgets::drawSearchField( "##HierarchyFilter", _filterBuffer,
											"Search (t:Mesh, tag:Player)...", 0.0f, false );
		}
		EditorChrome::endToolbar();

		ImGui::Separator();

		editor::EditorSectionDesc treeDesc{};
		treeDesc._pId  = "##HierarchyTree";
		treeDesc._kind = editor::EditorSectionKind::Child;
		if ( EditorChrome::beginSection( treeDesc ) )
		{
			for ( GameObject* pObj : listObjects )
			{
				if ( pObj != nullptr && pObj->getParent() == nullptr )
					HierarchyPanelInternal::drawGameObjectNode( pObj, pManager, _filterBuffer.c_str(), _renamingObjectId, _renameBuffer,
																_bFocusRenameInput );
			}

			// Empty area Drag & Drop Target for SW_ASSET_PATH
			if ( ImGui::BeginDragDropTarget() )
			{
				const ImGuiPayload* pGoPayload = ImGui::AcceptDragDropPayload( HierarchyPanelInternal::kHierarchyGoPayload );
				if ( pGoPayload != nullptr && pGoPayload->DataSize == static_cast<int32>( sizeof( uint64 ) ) )
				{
					const uint64	  draggedId = *static_cast<const uint64*>( pGoPayload->Data );
					GameObject* const pDragged	= pManager->findGameObjectById( draggedId );
					if ( pDragged != nullptr && pDragged->getParent() != nullptr )
						EditorSceneCommands::unparent( pDragged, "Detach GameObject to Root" );
				}

				string droppedAssetPath;
				if ( EditorWidgets::tryAcceptAssetPayload( droppedAssetPath ) )
					EditorAssetCommands::spawnPrefab( pManager, droppedAssetPath.c_str(), nullptr );
				ImGui::EndDragDropTarget();
			}

			// Keyboard shortcuts (Ctrl+D duplicate, F2 inline rename, Delete destroy)
			if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && ImGui::GetIO().WantTextInput == false )
			{
				const ImGuiIO&				 io		 = ImGui::GetIO();
				SelectionManager&			 selMgr	 = EditorContext::get()->getSelectionManager();
				const vector<GameObjectPtr>& listSel = selMgr.getSelectedObjects();

				if ( listSel.empty() == false )
				{
					if ( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D, false ) )
					{
						vector<GameObject*> listNewCreated;
						for ( const GameObjectPtr& pGoPtr : listSel )
						{
							GameObject* pSrc = pGoPtr.get();
							if ( pSrc != nullptr )
							{
								GameObject* pNewGo = EditorSceneCommands::duplicate( pManager, pSrc );
								if ( pNewGo != nullptr )
									listNewCreated.push_back( pNewGo );
							}
						}
						if ( listNewCreated.empty() == false )
						{
							selMgr.clearObjectSelection();
							for ( GameObject* pNewGo : listNewCreated )
								selMgr.selectObject( GameObjectPtr{ pNewGo }, SelectionMode::Add );
						}
					}
					else if ( ImGui::IsKeyPressed( ImGuiKey_F2, false ) )
					{
						GameObject* pSelected = listSel.back().get();
						if ( pSelected != nullptr )
						{
							_renamingObjectId = pSelected->getObjectId();
							formatstring( _renameBuffer.data(), _renameBuffer.capacity(), "%#", pSelected->getName().c_str() );
							_bFocusRenameInput = true;
						}
					}
					else if ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
					{
						for ( const GameObjectPtr& pGoPtr : listSel )
						{
							GameObject* pGo = pGoPtr.get();
							if ( pGo != nullptr )
								EditorSceneCommands::destroy( pManager, pGo );
						}
						selMgr.clearObjectSelection();
					}
				}
			}

			// 빈 공간 우클릭 메뉴
			if ( ImGui::BeginPopupContextWindow( "HierarchyEmptyCtx",
												 ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
			{
				if ( ImGui::MenuItem( "Create Empty GameObject" ) )
					EditorSceneCommands::create( pManager, nullptr );

				EditorContext::get()->getActionMenuManager().drawActionMenu( ActionMenuLocation::Hierarchy );
				ImGui::EndPopup();
			}
		}
		EditorChrome::endSection();
	}
} // namespace sw::editor
