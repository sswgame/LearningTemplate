#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"

namespace sw::editor
{
	void IEditorPanel::draw()
	{
		setNextPanelSize( getInitialPanelSize() );

		if ( beginPanel( getPanelTitle(), getOpenPtr(), getPanelFlags() ) == false )
		{
			onPanelCollapsed();
			endPanel();
			return;
		}

		drawContent();
		endPanel();
	}
} // namespace sw::editor
