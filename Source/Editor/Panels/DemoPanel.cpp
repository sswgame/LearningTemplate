/**
 * @file DemoPanel.cpp
 */
#include "Panels/DemoPanel.h"
#include <imgui.h>

namespace sw
{
	void DemoPanel::draw( const EditorUIContext& ctx )
	{
		if ( ctx.bShowDemoWindow == nullptr || *ctx.bShowDemoWindow == false )
			return;

		ImGui::ShowDemoWindow( ctx.bShowDemoWindow );
	}
}
