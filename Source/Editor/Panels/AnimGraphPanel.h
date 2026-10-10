#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorGraphDocumentPanel.h"

#include "Engine/Animation/Graph/AnimGraphAsset.h"
#include "Engine/Animation/Graph/AnimGraphPlayer.h"

namespace sw::editor
{
    /** @brief imgui-node-editor 로 만든 애니메이션 그래프 편집 패널입니다. */
    class AnimGraphPanel : public EditorGraphDocumentPanel<AnimGraphAsset>
    {
    public:
        /** @brief 애니메이션 그래프 도구를 생성합니다. */
        AnimGraphPanel();
        /** @brief 노드 에디터 컨텍스트를 해제합니다. */
        virtual ~AnimGraphPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 애니메이션 그래프 UI를 그립니다. */
        void drawContent() override;
        /** @brief 노드 추가·저장·줌 툴바를 그립니다. */
        void drawAnimationToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void               drawAnimationCanvas();
        [[nodiscard]] bool saveDocument() override;

    private:
        using GraphNode = NodeType;
        using GraphLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 그래프 조작 · JSON 로드/저장 (공통 뼈대는 EditorGraphDocumentPanel 에 있다)
        // ------------------------------------------------------------------------------
        /** @brief 기본 노드가 없으면 넣습니다. */
        void ensureDefaults() override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 그래프 데이터를 저장합니다. */
        [[nodiscard]] bool saveGraphData();
        /** @brief 주어진 이름의 노드를 추가하고 그 id 를 돌려줍니다. */
        int32 addNamedNode( const utf8* pName );
        /** @brief 핀 번호의 연결 정보입니다(포즈 타입 하나). 이 그래프의 핀이 아니면 false 입니다. */
        bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const;
        /** @brief 미리보기 플레이어에 현재 그래프를 넣습니다. */
        void syncPreviewGraph();
        /** @brief 미리보기 재생을 한 틱 진행합니다. */
        void tickPreview( float32 deltaSeconds );

    private:
        AnimGraphAsset  _previewGraph;  ///< 미리보기 플레이어가 빌려 쓰는 그래프 사본입니다
        AnimGraphPlayer _previewPlayer; ///< 노드 이름만 넘깁니다 — 클립 없이 "끝나면 다음" 을 손으로 진행합니다
    };
} // namespace sw::editor
