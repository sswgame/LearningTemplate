#pragma once
/**
 * @file PlotPanel.h
 * @brief ImPlot smoke / demo panel
 */
#include "IEditorPanel.h"

namespace sw
{
	class PlotPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Plot"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
} // namespace sw
