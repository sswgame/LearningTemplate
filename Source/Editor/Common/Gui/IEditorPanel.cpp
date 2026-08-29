#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Editor/Common/Gui/EditorChrome.h"

namespace sw::editor
{
	void IEditorPanel::draw()
	{
		EditorChrome::setNextPanelSize( getInitialPanelSize() );

		if ( EditorChrome::beginPanel( getPanelTitle(), getOpenPtr(), getPanelFlags() ) == false )
		{
			onPanelCollapsed();
			EditorChrome::endPanel();
			return;
		}

		drawContent();
		EditorChrome::endPanel();
	}
} // namespace sw::editor
