#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"

namespace sw::editor
{
	/** @brief imgui-node-editor 기반 비주얼 대화/퀘스트 노드 그래프 에디터 */
	class DialogueGraphPanel : public EditorDocumentPanel
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
		/** @brief 대화 노드 그래프 UI를 렌더링합니다. */
		void drawContent() override;
		bool saveDocument() override;

	private:
		using DialogueNode = EditorDialogueNode;
		using DialogueLink = EditorDialogueLink;

		string captureDocumentText() const override;
		void   applyDocumentText( string_view text ) override;

		// ------------------------------------------------------------------------------
		// 2) 내부 처리 함수
		// ------------------------------------------------------------------------------
		/** @brief 기본 샘플 노드들을 구성합니다. */
		void ensureDefaults();
		/** @brief 대화 그래프 JSON 파일을 불러옵니다. */
		void loadGraphData();
		/** @brief 대화 그래프를 JSON 파일로 저장합니다. */
		void saveGraphData();

		/** @brief 새 노드 ID를 발급합니다. */
		int32 nextNodeId() const;
		/** @brief 새 링크 ID를 발급합니다. */
		int32 nextLinkId() const;

		/** @brief 지정한 타입의 노드를 추가합니다. */
		void addNode( DialogueNodeType type, const utf8* pSpeaker = "", const utf8* pText = "" );
		/** @brief 현재 목록을 파일 데이터로 만듭니다. */
		EditorDialogueGraphData captureGraphData() const;
		/** @brief 노드 위치를 캐시하고 이동이면 dirty로 표시합니다. */
		void cacheNodeLayout();
		/** @brief 미리보기 재생을 한 틱 진행합니다. */
		void tickPreview( float32 deltaSeconds );
		/** @brief 미리보기를 다음 노드로 보냅니다. */
		void previewAdvance( int32 pinOffset = 2 );
		/** @brief 미리보기 툴바를 그립니다. */
		void drawPreviewToolbar();

	private:
		EditorNodeGraph		   _nodeGraph;
		int32				   _selectedNodeId;
		vector<DialogueNode>   _listNode;
		vector<DialogueLink>   _listLink;
		int32				   _previewNodeId;
		float32				   _previewHoldSeconds;
		uint8				   _bGraphLayoutReady : 1;
		uint8				   _bPreviewPlaying	  : 1;
		[[maybe_unused]] uint8 _reservedGraph	  : 6;
	};
} // namespace sw::editor
