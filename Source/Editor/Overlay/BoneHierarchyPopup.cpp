/**
 * @file BoneHierarchyPopup.cpp
 */
#include "Overlay/BoneHierarchyPopup.h"
#include "Workspace/EditorWorkspace.h"
#include <imgui.h>

namespace sw
{
	void drawBoneHierarchyPopup()
	{
		if ( editor::boneHierarchyPopupOpen() == false )
			return;

		if ( ImGui::Begin( "Bone Hierarchy", &editor::boneHierarchyPopupOpen() ) == false )
		{
			ImGui::End();
			return;
		}

		const std::string& name = editor::selectedObjectName();
		if ( name.empty() )
			ImGui::TextDisabled( "No selection" );
		else
			ImGui::Text( "Selection: %s", name.c_str() );

		ImGui::Separator();
		ImGui::TextDisabled( "No skeleton API on selection — stub tree" );
		if ( ImGui::TreeNodeEx( "Root", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			if ( ImGui::TreeNodeEx( "Hips", ImGuiTreeNodeFlags_DefaultOpen ) )
			{
				if ( ImGui::TreeNodeEx( "Spine", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf ) )
					ImGui::TreePop();
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		ImGui::End();
	}
} // namespace sw
