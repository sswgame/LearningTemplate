/**
 * @file NotifyPanel.cpp
 */
#include "Panels/NotifyPanel.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>

#define NOTIFY_RENDER_OUTSIDE_MAIN_WINDOW false
#if defined( SW_PLATFORM_LINUX )
	#if defined( Success )
		#undef Success
		#include <ImGuiNotify.hpp>
	#endif
#else
	#include <ImGuiNotify.hpp>
#endif

namespace sw
{
	void NotifyPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted( "ImGuiNotify toasts (rendered in editor shell each frame)." );
		if ( ImGui::Button( "Success" ) )
			ImGui::InsertNotification( { ImGuiToastType::Success, 3000, "That worked." } );
		ImGui::SameLine();
		if ( ImGui::Button( "Warning" ) )
			ImGui::InsertNotification( { ImGuiToastType::Warning, 3000, "Something looks off." } );
		ImGui::SameLine();
		if ( ImGui::Button( "Error" ) )
			ImGui::InsertNotification( { ImGuiToastType::Error, 3000, "Failure: 0x%X", 0xDEADBEEF } );
		ImGui::SameLine();
		if ( ImGui::Button( "Info" ) )
			ImGui::InsertNotification( { ImGuiToastType::Info, 3000, "FYI from Notify panel." } );

		ImGui::End();
	}
} // namespace sw
