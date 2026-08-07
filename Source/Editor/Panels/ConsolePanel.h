#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class ConsolePanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Live Coding & Console Log"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
}
