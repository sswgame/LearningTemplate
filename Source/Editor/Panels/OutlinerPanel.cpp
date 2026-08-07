/**
 * @file OutlinerPanel.cpp
 * @brief Hierarchy outliner (GameObject / Component)
 */
#include "Panels/OutlinerPanel.h"
#include "EditorSelection.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Object/SceneComponent.h"
#include <imgui.h>
#include <cstdio>

namespace sw
{
	namespace
	{
		void selectObject( GameObject* obj )
		{
			if ( obj == nullptr )
				return;
			editor::selectedObjectId()	  = obj->getObjectId();
			editor::selectedComponentId() = 0;
			editor::selectedObjectName()	  = obj->getName().c_str();
		}

		void selectComponent( GameObject* obj, Component* comp )
		{
			if ( obj == nullptr || comp == nullptr )
				return;
			editor::selectedObjectId()	  = obj->getObjectId();
			editor::selectedComponentId() = comp->getComponentId();
			editor::selectedObjectName()	  = obj->getName().c_str();
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

			const std::vector<hashed_string>& types = getComponentManager().getRegisteredComponentTypes();
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
			{
				GameObject* created = manager->createGameObject( hashed_string( "GameObject" ) );
				selectObject( created );
			}

			drawAddComponentMenu( obj );

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

		void drawGameObjectNode( GameObject* obj, GameObjectManager* manager )
		{
			if ( obj == nullptr )
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

			if ( bOpen )
			{
				for ( GameObject* child : obj->getChildren() )
					drawGameObjectNode( child, manager );

				// Non-SceneComponents as flat selectables; SceneComponents as hierarchy roots.
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

	void OutlinerPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		Scene* scene = getSceneManager().getActiveScene();
		if ( scene == nullptr || scene->getObjectManager() == nullptr )
		{
			ImGui::TextDisabled( "No active scene." );
			ImGui::End();
			return;
		}

		GameObjectManager* manager = scene->getObjectManager();
		const auto&		   objects = manager->getAllGameObjects();

		ImGui::Text( "Scene: %s (%u objects)", scene->getName().c_str(), static_cast<uint32>( objects.size() ) );
		ImGui::Separator();

		if ( ImGui::Button( "Create GameObject" ) )
		{
			GameObject* created = manager->createGameObject( hashed_string( "GameObject" ) );
			selectObject( created );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Clear Selection" ) )
			editor::clearSelection();

		ImGui::BeginChild( "##HierarchyTree", ImVec2( 0, 0 ), ImGuiChildFlags_None );

		if ( ImGui::BeginPopupContextWindow( "HierarchyBlankCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
		{
			if ( ImGui::MenuItem( "Create GameObject" ) )
			{
				GameObject* created = manager->createGameObject( hashed_string( "GameObject" ) );
				selectObject( created );
			}
			ImGui::EndPopup();
		}

		for ( GameObject* obj : objects )
		{
			if ( obj != nullptr && obj->getParent() == nullptr )
				drawGameObjectNode( obj, manager );
		}

		ImGui::EndChild();
		ImGui::End();
	}
} // namespace sw
