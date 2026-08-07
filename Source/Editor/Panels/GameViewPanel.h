#pragma once
#include "Panels/IEditorPanel.h"

namespace sw
{
	class GameViewPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Game View"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
}
