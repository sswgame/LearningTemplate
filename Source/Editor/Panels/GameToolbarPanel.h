#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class GameToolbarPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Game Toolbar"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
}
