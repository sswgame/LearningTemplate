#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"

namespace sw::editor
{
	/** @brief 대화 노드 타입 */
	enum class DialogueNodeType : uint8
	{
		Start = 0,
		Dialogue,
		Choice,
		Branch,
		Action,
		End
	};

	/** @brief imgui-node-editor 기반 비주얼 대화/퀘스트 노드 그래프 에디터 */
	class DialogueGraphPanel : public IEditorPanel
	{
	public:
		/** @brief 대화 그래프 도구를 생성합니다. */
		DialogueGraphPanel();
		/** @brief 노드 에디터 컨텍스트를 해제합니다. */
		virtual ~DialogueGraphPanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 수명주기 및 UI 렌더링
		// ------------------------------------------------------------------------------
		void shutdown( IRHIDevice* pRhiDevice ) override;
		/** @brief 온디맨드 패널이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }
		/** @brief 대화 노드 그래프 UI를 렌더링합니다. */
		void drawContent() override;
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Dialogue Graph"; }

	private:
		// ------------------------------------------------------------------------------
		// 2) 노드/링크 데이터 구조체
		// ------------------------------------------------------------------------------
		/** @brief 대화 그래프 노드 */
		struct DialogueNode
		{
			int32			 _id{ 0 };
			DialogueNodeType _type{ DialogueNodeType::Dialogue };
			string			 _speaker;
			string			 _text;
			string			 _condition;
			string			 _actionCommand;
			vector<string>	 _listChoice;
			float32			 _x{ 40.0f };
			float32			 _y{ 40.0f };
		};

		/** @brief 핀 간의 연결 링크 */
		struct DialogueLink
		{
			int32 _id{ 0 };
			int32 _fromPin{ 0 };
			int32 _toPin{ 0 };
		};

		// ------------------------------------------------------------------------------
		// 3) 내부 처리 함수
		// ------------------------------------------------------------------------------
		/** @brief 기본 샘플 노드들을 구성합니다. */
		void ensureDefaults();
		/** @brief 대화 그래프 JSON 파일을 불러옵니다. */
		void loadGraphData();
		/** @brief 대화 그래프를 JSON 파일로 저장합니다. */
		void saveGraphData() const;

		/** @brief 새 노드 ID를 발급합니다. */
		int32 nextNodeId() const;
		/** @brief 새 링크 ID를 발급합니다. */
		int32 nextLinkId() const;

		/** @brief 지정한 타입의 노드를 추가합니다. */
		void addNode( DialogueNodeType type, const utf8* pSpeaker = "", const utf8* pText = "" );

	private:
		EditorNodeGraph		 _nodeGraph;
		bool				 _bLoaded;
		int32				 _selectedNodeId;
		vector<DialogueNode> _listNode;
		vector<DialogueLink> _listLink;
	};
} // namespace sw::editor
