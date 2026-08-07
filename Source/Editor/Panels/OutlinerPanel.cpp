/**
 * @file OutlinerPanel.cpp
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
#include "Core/Object/SceneComponent.h"
#include <imgui.h>
#include <cstdio>

namespace sw
{
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

		if ( ImGui::Button( "Clear Selection" ) )
			editor::clearSelection();

		ImGui::BeginChild( "##HierarchyTree", ImVec2( 0, 0 ), ImGuiChildFlags_None );

		for ( GameObject* obj : objects )
		{
			if ( obj == nullptr )
				continue;

			ImGui::PushID( static_cast<int>( obj->getObjectId() ) );

			const bool bSelected = ( editor::selectedObjectId() == obj->getObjectId() &&
									 editor::selectedComponentId() == 0 );

			char label[256];
			std::snprintf( label, sizeof( label ), "%s##go%llu",
						   obj->getName().c_str(),
						   static_cast<unsigned long long>( obj->getObjectId() ) );

			const bool bOpen = ImGui::TreeNodeEx(
				label,
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
					( bSelected ? ImGuiTreeNodeFlags_Selected : 0 ) |
					( obj->getComponentCount() == 0 ? ImGuiTreeNodeFlags_Leaf : 0 ) );

			if ( ImGui::IsItemClicked() )
			{
				editor::selectedObjectId()	  = obj->getObjectId();
				editor::selectedComponentId() = 0;
			}

			if ( bOpen )
			{
				for ( Component* comp : obj->getAllComponents() )
				{
					if ( comp == nullptr )
						continue;

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
					{
						editor::selectedObjectId()	  = obj->getObjectId();
						editor::selectedComponentId() = comp->getComponentId();
					}

					if ( SceneComponent* sceneComp = comp->asSceneComponent() )
					{
						if ( sceneComp->getParent() != nullptr )
						{
							ImGui::SameLine();
							ImGui::TextDisabled( "(child)" );
						}
					}

					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		ImGui::EndChild();
		ImGui::End();
	}
} // namespace sw
