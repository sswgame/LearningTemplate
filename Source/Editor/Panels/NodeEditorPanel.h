#pragma once
/**
 * @file NodeEditorPanel.h
 * @brief imgui-node-editor (thedmd) smoke panel
 */
#include "IEditorPanel.h"
#include "Core/Common/CommonHeaders.h"

namespace ax
{
	namespace NodeEditor
	{
		struct EditorContext;
	}
} // namespace ax

namespace sw
{
	class NodeEditorPanel : public IEditorPanel
	{
	public:
		NodeEditorPanel();
		~NodeEditorPanel() override;

		const char* getWindowTitle() const override { return "Node Editor"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		void ensureEditor();
		void destroyEditor();

		ax::NodeEditor::EditorContext* _editor = nullptr;
		/** @brief ed::Config::SettingsFile 이 가리키는 경로 (수명 유지) */
		std::string					   _settingsPath;
	};
} // namespace sw
