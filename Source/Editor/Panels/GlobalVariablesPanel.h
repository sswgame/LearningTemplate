#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class GlobalVariablesPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Global Variables Control"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		char _filterBuffer[128] = {};
	};
}
