#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class DemoPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Dear ImGui Demo"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
}
