#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"

namespace sw
{
	void IEditorPanel::draw()
	{
		editor::setNextPanelSize( getInitialPanelSize() );

		if ( editor::beginPanel( getPanelTitle(), getOpenPtr(), getPanelFlags() ) == false )
		{
			onPanelCollapsed();
			editor::endPanel();
			return;
		}

		drawContent();
		editor::endPanel();
	}
} // namespace sw
