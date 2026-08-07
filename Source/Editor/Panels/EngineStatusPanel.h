#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class EngineStatusPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Engine RHI Status & Command Line"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
}
