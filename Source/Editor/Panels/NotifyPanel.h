#pragma once
/**
 * @file NotifyPanel.h
 * @brief imgui-notify (ImGuiNotify) toast smoke panel
 */
#include "IEditorPanel.h"

namespace sw
{
	class NotifyPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Notify"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		bool _bOpen = true;
	};
} // namespace sw
