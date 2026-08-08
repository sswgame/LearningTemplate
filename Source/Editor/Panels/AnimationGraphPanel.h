#pragma once
/**
 * @file AnimationGraphPanel.h
 * @brief Animation graph editor shell (imgui-node-editor)
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
	class AnimationGraphPanel : public IEditorPanel
	{
	public:
		AnimationGraphPanel();
		~AnimationGraphPanel() override;

		const char* getWindowTitle() const override { return "Animation Graph"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		void ensureEditor();
		void destroyEditor();

		ax::NodeEditor::EditorContext* _editor = nullptr;
		std::string					   _settingsPath;
	};
} // namespace sw
