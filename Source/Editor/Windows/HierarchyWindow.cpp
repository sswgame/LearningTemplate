/**
 * @file HierarchyWindow.cpp
 * @brief Hierarchy outliner (GameObject / Component)
 */
#include "Windows/HierarchyWindow.h"
#include "Workspace/EditorWorkspace.h"
#include "Workspace/EditorCommandStack.h"
#include "Workspace/EditorAssetDrop.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/GameState.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Utility/Log/Logger.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace sw
{
	namespace
	{
		constexpr const char* kHierarchyGoPayload = "SW_HIERARCHY_GO";

		void selectObject( GameObject* obj )
		{
			editor::selectGameObject( obj );
		}

		bool nameMatchesFilter( const std::string& name, const char* filter )
		{
			if ( filter == nullptr || filter[0] == '\0' )
				return true;
			std::string hay = name;
			std::string needle = filter;
			std::transform( hay.begin(), hay.end(), hay.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );
			std::transform( needle.begin(), needle.end(), needle.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );
			return hay.find( needle ) != std::string::npos;
		}

		bool subtreeMatchesFilter( GameObject* obj, const char* filter )
		{
			if ( obj == nullptr )
				return false;
			if ( nameMatchesFilter( obj->getName().c_str(), filter ) )
				return true;
			for ( GameObject* child : obj->getChildren() )
			{
				if ( subtreeMatchesFilter( child, filter ) )
					return true;
			}
			return false;
		}

		GameObject* createGameObjectWithRoot( GameObjectManager* manager, GameObject* parent )
		{
			if ( manager == nullptr )
				return nullptr;

			GameObject* created = manager->createGameObject( hashed_string( "GameObject" ) );
			if ( created == nullptr )
				return nullptr;

			created->addComponent<SceneComponent>();
			if ( parent != nullptr )
				created->attachToParent( parent );

			const uint64 objectId = created->getObjectId();
			EditorCommandStack::Command cmd;
			cmd.label = "Create GameObject";
			cmd.undo  = [objectId]()
			{
				Scene* scene = core::getSceneManager().getActiveScene();
				if ( scene == nullptr || scene->getObjectManager() == nullptr )
					return;
				GameObjectManager* mgr = scene->getObjectManager();
				GameObject*		   obj = mgr->findGameObjectById( objectId );
				if ( obj == nullptr )
					return;
				if ( editor::selectedObjectId() == objectId )
					editor::clearSelection();
				mgr->destroyObjectDeferred( obj );
				mgr->processDeferredDestruction();
			};
			cmd.redo = [objectId]()
			{
				// Object already exists after create; redo after undo cannot restore id-stable object.
				// Re-create a fresh GO with SceneComponent root as best-effort redo.
				(void)objectId;
				Scene* scene = core::getSceneManager().getActiveScene();
				if ( scene == nullptr || scene->getObjectManager() == nullptr )
					return;
				GameObjectManager* mgr	  = scene->getObjectManager();
				GameObject*		   again  = mgr->createGameObject( hashed_string( "GameObject" ) );
				if ( again != nullptr )
				{
					again->addComponent<SceneComponent>();
					editor::selectGameObject( again );
				}
			};
			EditorCommandStack::get().push( std::move( cmd ) );

			selectObject( created );
			return created;
		}

		bool wouldCreateParentCycle( GameObject* child, GameObject* newParent )
		{
			if ( child == nullptr || newParent == nullptr || child == newParent )
				return true;

			GameObject* ancestor = newParent;
			while ( ancestor != nullptr )
			{
				if ( ancestor == child )
					return true;
				ancestor = ancestor->getParent();
			}
			return false;
		}

		void handleHierarchyReparentDrop( GameObject* targetParent, const ImGuiPayload* payload, GameObjectManager* manager )
		{
			if ( payload == nullptr || manager == nullptr || targetParent == nullptr )
				return;
			if ( payload->DataSize != static_cast<int>( sizeof( uint64 ) ) )
				return;

			const uint64		  draggedId = *static_cast<const uint64*>( payload->Data );
			GameObject* const dragged	  = manager->findGameObjectById( draggedId );
			if ( dragged == nullptr || dragged == targetParent )
				return;
			if ( wouldCreateParentCycle( dragged, targetParent ) )
				return;

			if ( dragged->attachToParent( targetParent ) )
				selectObject( dragged );
		}

		void drawGameObjectDragDrop( GameObject* obj, GameObjectManager* manager )
		{
			if ( obj == nullptr || manager == nullptr )
				return;

			if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
			{
				const uint64 id = obj->getObjectId();
				ImGui::SetDragDropPayload( kHierarchyGoPayload, &id, sizeof( id ) );
				ImGui::TextUnformatted( obj->getName().c_str() );
				ImGui::EndDragDropSource();
			}

			if ( ImGui::BeginDragDropTarget() )
			{
				if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kHierarchyGoPayload ) )
					handleHierarchyReparentDrop( obj, payload, manager );

				if ( const ImGuiPayload* assetPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" ) )
				{
					const char* path = static_cast<const char*>( assetPayload->Data );
					if ( path != nullptr )
					{
						if ( GameObject* spawned = editor::spawnPrefabFromAssetPath( manager, path, obj ) )
							selectObject( spawned );
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		void selectComponent( GameObject* obj, Component* comp )
		{
			editor::selectComponent( obj, comp );
		}

		void drawComponentContextMenu( GameObject* obj, Component* comp, GameObjectManager* manager )
		{
			if ( ImGui::BeginPopupContextItem( "CompCtx" ) == false )
				return;

			if ( ImGui::MenuItem( "Select Owner GameObject" ) )
				selectObject( obj );

			if ( ImGui::MenuItem( "Remove Component" ) && comp != nullptr && manager != nullptr )
			{
				if ( editor::selectedObjectId() == obj->getObjectId() &&
					 editor::selectedComponentId() == comp->getComponentId() )
				{
					editor::selectedComponentId() = 0;
					editor::selectedComponentKey().clear();
				}
				manager->destroyComponentDeferred( comp );
				manager->processDeferredDestruction();
			}

			ImGui::EndPopup();
		}

		void drawAddComponentMenu( GameObject* obj )
		{
			if ( ImGui::BeginMenu( "Add Component" ) == false )
				return;

			const std::vector<hashed_string>& types = core::getComponentManager().getRegisteredComponentTypes();
			if ( types.empty() )
			{
				ImGui::TextDisabled( "No registered component types." );
			}
			else
			{
				for ( const hashed_string& typeName : types )
				{
					if ( ImGui::MenuItem( typeName.c_str() ) )
					{
						if ( obj->addComponentByName( typeName ) == nullptr )
							ImGui::OpenPopup( "AddCompFailed" );
					}
				}
			}
			ImGui::EndMenu();
		}

		void drawGameObjectContextMenu( GameObject* obj, GameObjectManager* manager )
		{
			if ( ImGui::BeginPopupContextItem( "GOCtx" ) == false )
				return;

			if ( ImGui::MenuItem( "Create GameObject" ) )
				createGameObjectWithRoot( manager, nullptr );

			if ( ImGui::MenuItem( "Create Child GameObject" ) )
				createGameObjectWithRoot( manager, obj );

			drawAddComponentMenu( obj );

			ImGui::Separator();

			if ( obj->getParent() != nullptr )
			{
				if ( ImGui::MenuItem( "Unparent" ) )
					obj->detachFromParent();
			}

			GameObject* selected = manager->findGameObjectById( editor::selectedObjectId() );
			const bool	bCanParentToSelected =
				selected != nullptr && selected != obj && editor::selectedComponentId() == 0;
			if ( bCanParentToSelected )
			{
				bool		bWouldCycle = false;
				GameObject* ancestor	= selected;
				while ( ancestor != nullptr )
				{
					if ( ancestor == obj )
					{
						bWouldCycle = true;
						break;
					}
					ancestor = ancestor->getParent();
				}

				if ( bWouldCycle == false )
				{
					if ( ImGui::MenuItem( "Parent to Selected" ) )
						obj->attachToParent( selected );
				}
				else
				{
					ImGui::BeginDisabled();
					ImGui::MenuItem( "Parent to Selected" );
					ImGui::EndDisabled();
				}
			}

			ImGui::Separator();
			if ( ImGui::MenuItem( "Destroy GameObject" ) )
			{
				if ( editor::selectedObjectId() == obj->getObjectId() )
					editor::clearSelection();
				manager->destroyObjectDeferred( obj );
				manager->processDeferredDestruction();
			}

			ImGui::EndPopup();
		}

		void drawSceneComponentNode( GameObject* obj, SceneComponent* sceneComp, GameObjectManager* manager )
		{
			if ( obj == nullptr || sceneComp == nullptr )
				return;

			ImGui::PushID( static_cast<int>( sceneComp->getComponentId() ) );

			const bool bSelected = ( editor::selectedObjectId() == obj->getObjectId() &&
									 editor::selectedComponentId() == sceneComp->getComponentId() );

			const char* compName = sceneComp->getComponentName().empty() == false
									   ? sceneComp->getComponentName().c_str()
									   : "SceneComponent";

			char label[256];
			std::snprintf( label, sizeof( label ), "%s##sc%llu",
						   compName,
						   static_cast<unsigned long long>( sceneComp->getComponentId() ) );

			bool		 hasChildOnOwner = false;
			const auto& children		 = sceneComp->getChildren();
			for ( SceneComponent* child : children )
			{
				if ( child != nullptr && child->getOwner() == obj )
				{
					hasChildOnOwner = true;
					break;
				}
			}

			const ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
				( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) |
				( hasChildOnOwner ? 0 : ImGuiTreeNodeFlags_Leaf );

			const bool bOpen = ImGui::TreeNodeEx( label, flags );
			if ( ImGui::IsItemClicked() )
				selectComponent( obj, sceneComp );
			drawComponentContextMenu( obj, sceneComp, manager );

			if ( bOpen )
			{
				for ( SceneComponent* child : children )
				{
					if ( child != nullptr && child->getOwner() == obj )
						drawSceneComponentNode( obj, child, manager );
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		void drawGameObjectNode( GameObject* obj, GameObjectManager* manager, const char* filter )
		{
			if ( obj == nullptr )
				return;
			if ( subtreeMatchesFilter( obj, filter ) == false )
				return;

			ImGui::PushID( static_cast<int>( obj->getObjectId() ) );

			const bool bSelected = ( editor::selectedObjectId() == obj->getObjectId() &&
									 editor::selectedComponentId() == 0 );

			char label[256];
			std::snprintf( label, sizeof( label ), "%s##go%llu",
						   obj->getName().c_str(),
						   static_cast<unsigned long long>( obj->getObjectId() ) );

			const bool bHasChildGos	  = obj->getChildren().empty() == false;
			const bool bHasComponents = obj->getComponentCount() > 0;
			const bool bLeaf		  = ( bHasChildGos == false && bHasComponents == false );

			const bool bOpen = ImGui::TreeNodeEx(
				label,
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
					( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) |
					( bLeaf ? ImGuiTreeNodeFlags_Leaf : 0 ) );

			if ( ImGui::IsItemClicked() )
				selectObject( obj );
			drawGameObjectContextMenu( obj, manager );
			drawGameObjectDragDrop( obj, manager );

			if ( bOpen )
			{
				for ( GameObject* child : obj->getChildren() )
					drawGameObjectNode( child, manager, filter );

				for ( Component* comp : obj->getAllComponents() )
				{
					if ( comp == nullptr )
						continue;

					SceneComponent* sceneComp = comp->asSceneComponent();
					if ( sceneComp != nullptr )
					{
						const SceneComponent* parent = sceneComp->getParent();
						const bool			  bRootOnThisGo =
							parent == nullptr || parent->getOwner() != obj;
						if ( bRootOnThisGo )
							drawSceneComponentNode( obj, sceneComp, manager );
						continue;
					}

					ImGui::PushID( static_cast<int>( comp->getComponentId() ) );

					const bool bCompSelected = ( editor::selectedObjectId() == obj->getObjectId() &&
												 editor::selectedComponentId() == comp->getComponentId() );

					const char* compName = comp->getComponentName().empty() == false
											   ? comp->getComponentName().c_str()
											   : "Component";

					char compLabel[256];
					std::snprintf( compLabel, sizeof( compLabel ), "%s##c%llu",
								   compName,
								   static_cast<unsigned long long>( comp->getComponentId() ) );

					if ( ImGui::Selectable( compLabel, bCompSelected ) )
						selectComponent( obj, comp );
					drawComponentContextMenu( obj, comp, manager );

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

		Scene* scene = core::getSceneManager().getActiveScene();
		if ( scene == nullptr || scene->getObjectManager() == nullptr )
		{
			ImGui::TextDisabled( "No active scene." );
			ImGui::End();
			return;
		}

		GameObjectManager* manager = scene->getObjectManager();
		const auto&		   objects = manager->getAllGameObjects();

		ImGui::Text( "Scene: %s (%u objects)", scene->getName().c_str(), static_cast<uint32>( objects.size() ) );
		ImGui::InputTextWithHint( "##hier_filter", "Search...", _filterBuffer, sizeof( _filterBuffer ) );
		ImGui::Separator();

		const bool bPlaying = ( getGameState() == GameState::Playing );
		if ( bPlaying )
			ImGui::BeginDisabled();

		if ( ImGui::Button( "Create GameObject" ) )
		{
			GameObject* parent = nullptr;
			GameObject* selected = manager->findGameObjectById( editor::selectedObjectId() );
			if ( selected != nullptr && editor::selectedComponentId() == 0 )
				parent = selected;
			createGameObjectWithRoot( manager, parent );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Clear Selection" ) )
			editor::clearSelection();

		ImGui::BeginChild( "##HierarchyTree", ImVec2( 0, 0 ), ImGuiChildFlags_None );

		if ( ImGui::BeginDragDropTarget() )
		{
			if ( const ImGuiPayload* assetPayload = ImGui::AcceptDragDropPayload( "SW_ASSET_PATH" ) )
			{
				const char* path = static_cast<const char*>( assetPayload->Data );
				if ( path != nullptr )
				{
					if ( GameObject* spawned = editor::spawnPrefabFromAssetPath( manager, path, nullptr ) )
						selectObject( spawned );
				}
			}
			ImGui::EndDragDropTarget();
		}

		if ( ImGui::BeginPopupContextWindow( "HierarchyBlankCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
		{
			if ( ImGui::MenuItem( "Create GameObject" ) )
				createGameObjectWithRoot( manager, nullptr );
			ImGui::EndPopup();
		}

		for ( GameObject* obj : objects )
		{
			if ( obj != nullptr && obj->getParent() == nullptr )
				drawGameObjectNode( obj, manager, _filterBuffer );
		}

		ImGui::EndChild();
		if ( bPlaying )
			ImGui::EndDisabled();
		ImGui::End();
	}
} // namespace sw
