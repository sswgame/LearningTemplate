#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"

namespace sw::editor
{
	/** @brief imgui-node-editor 기반 애니메이션 그래프 셸 */
	class AnimationGraphPanel : public EditorDocumentPanel
	{
	public:
		/** @brief 애니메이션 그래프 도구를 생성합니다. */
		AnimationGraphPanel();
		/** @brief 노드 에디터 컨텍스트를 해제합니다. */
		virtual ~AnimationGraphPanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		void shutdown( IRHIDevice* pRhiDevice ) override;
		/** @brief 애니메이션 그래프 UI를 그립니다. */
		void drawContent() override;
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Animation Graph"; }

	private:
		using GraphNode = EditorAnimGraphNode;
		using GraphLink = EditorAnimGraphLink;

		// ------------------------------------------------------------------------------
		// 2) 그래프 조작 · JSON 로드/저장
		// ------------------------------------------------------------------------------
		/** @brief 기본 노드가 없으면 넣습니다. */
		void ensureDefaults();
		/** @brief 그래프 데이터를 불러옵니다. */
		void loadGraphData();
		/** @brief 그래프 데이터를 저장합니다. */
		void saveGraphData();
		/** @brief 다음에 쓸 노드 ID를 반환합니다. */
		int32 nextNodeId() const;
		/** @brief 다음에 쓸 링크 ID를 반환합니다. */
		int32 nextLinkId() const;
		/** @brief 지정 이름의 노드를 추가합니다. */
		void addNamedNode( const utf8* pName );

	private:
		EditorNodeGraph	  _nodeGraph;
		vector<GraphNode> _listNode;
		vector<GraphLink> _listLink;
	};
} // namespace sw::editor
