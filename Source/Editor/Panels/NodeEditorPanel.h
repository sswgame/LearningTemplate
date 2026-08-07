#pragma once
/**
 * @file NodeEditorPanel.h
 * @brief imgui-node-editor (thedmd) smoke panel
 */
#include "IEditorPanel.h"

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

		bool						_bOpen	 = true;
		ax::NodeEditor::EditorContext* _editor = nullptr;
	};
} // namespace sw
