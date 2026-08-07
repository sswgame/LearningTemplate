/**
 * @file PlotPanel.cpp
 */
#include "Panels/PlotPanel.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <implot.h>


namespace sw
{
	void PlotPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		static float xs[64];
		static float ys[64];
		static bool	 bInit = false;
		if ( bInit == false )
		{
			for ( int i = 0; i < 64; ++i )
			{
				xs[i] = static_cast<float>( i ) * 0.1f;
				ys[i] = std::sin( xs[i] );
			}
			bInit = true;
		}

		if ( ImPlot::BeginPlot( "Sample Sin", ImVec2( -1, -1 ) ) )
		{
			ImPlot::PlotLine( "sin(x)", xs, ys, 64 );
			ImPlot::EndPlot();
		}

		ImGui::End();
	}
} // namespace sw
