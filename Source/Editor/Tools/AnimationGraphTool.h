#pragma once
/**
 * @file AnimationGraphTool.h
 * @brief Animation graph editor shell (imgui-node-editor)
 */
#include "Windows/IEditorWindow.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace ax
{
	namespace NodeEditor
	{
		struct EditorContext;
	}
} // namespace ax

namespace sw
{
	class AnimationGraphTool : public IEditorWindow
	{
	public:
		AnimationGraphTool();
		bool isToolWindow() const override { return true; }
		~AnimationGraphTool() override;

		const char* getWindowTitle() const override { return "Animation Graph"; }
		void		draw( const EditorUIContext& ctx ) override;
		void		shutdown( IRHIDevice* rhiDevice ) override;

	private:
		struct GraphNode
		{
			int32		id = 0;
			std::string name;
			float32		x = 40.0f;
			float32		y = 40.0f;
		};

		struct GraphLink
		{
			int32 id		= 0;
			int32 fromNode	= 0;
			int32 toNode	= 0;
		};

		void ensureEditor();
		void destroyEditor();
		void ensureDefaults();
		void loadGraphData();
		void saveGraphData() const;
		int32 nextNodeId() const;
		int32 nextLinkId() const;
		void  addNamedNode( const char* name );

		ax::NodeEditor::EditorContext* _editor			   = nullptr;
		std::string					   _settingsPath;
		bool						   _bNavigatedToContent = false;
		bool						   _bLoaded				= false;
		std::vector<GraphNode>		   _nodes;
		std::vector<GraphLink>		   _links;
	};
} // namespace sw
