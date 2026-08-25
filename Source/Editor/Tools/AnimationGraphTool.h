#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Tools/BaseNodeGraphEditor.h"

namespace sw
{
	/** @brief imgui-node-editor 기반 애니메이션 그래프 셸 */
	class AnimationGraphTool : public BaseNodeGraphEditor
	{
	public:
		/** @brief 애니메이션 그래프 도구를 생성합니다. */
		AnimationGraphTool();
		/** @brief 노드 에디터 컨텍스트를 해제합니다. */
		virtual ~AnimationGraphTool() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorWindow — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 애니메이션 그래프 UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getWindowTitle() const override { return "Animation Graph"; }

	private:
		// ------------------------------------------------------------------------------
		// 2) 그래프 데이터 — 노드 / 링크
		// ------------------------------------------------------------------------------
		/** @brief 그래프 노드 (id / 이름 / 캔버스 위치) */
		struct GraphNode
		{
			int32	_id{ 0 };
			string	_name;
			float32 _x{ 40.0f };
			float32 _y{ 40.0f };
		};

		/** @brief 노드 간 링크 */
		struct GraphLink
		{
			int32 _id{ 0 };
			int32 _fromNode{ 0 };
			int32 _toNode{ 0 };
		};

		// ------------------------------------------------------------------------------
		// 3) 그래프 조작 · JSON 로드/저장
		// ------------------------------------------------------------------------------
		/** @brief 기본 노드가 없으면 넣습니다. */
		void ensureDefaults();
		/** @brief 그래프 데이터를 불러옵니다. */
		void loadGraphData();
		/** @brief 그래프 데이터를 저장합니다. */
		void saveGraphData() const;
		/** @brief 다음에 쓸 노드 ID를 반환합니다. */
		int32 nextNodeId() const;
		/** @brief 다음에 쓸 링크 ID를 반환합니다. */
		int32 nextLinkId() const;
		/** @brief 지정 이름의 노드를 추가합니다. */
		void addNamedNode( const utf8* pName );

	private:
		bool			  _bLoaded;
		vector<GraphNode> _listNodes;
		vector<GraphLink> _listLinks;
	};

} // namespace sw
