#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/EditorGraphDocumentPanel.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Animation/AnimationGraphPlayer.h"

namespace sw::editor
{
    /** @brief imgui-node-editor 로 만든 애니메이션 그래프 편집 패널입니다. */
    class AnimationGraphPanel : public EditorGraphDocumentPanel<AnimationGraphAsset>
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
        /** @brief 노드 추가·저장·줌 툴바를 그립니다. */
        void drawAnimationToolbar();
        /** @brief 노드 그래프 캔버스를 그립니다. */
        void drawAnimationCanvas();
        bool saveDocument() override;

    private:
        using GraphNode = NodeType;
        using GraphLink = LinkType;

        // ------------------------------------------------------------------------------
        // 2) 그래프 조작 · JSON 로드/저장 (공통 뼈대는 EditorGraphDocumentPanel 에 있다)
        // ------------------------------------------------------------------------------
        /** @brief 기본 노드가 없으면 넣습니다. */
        void ensureDefaults() override;
        /** @brief 그래프 데이터를 불러옵니다. */
        void loadGraphData();
        /** @brief 그래프 데이터를 저장합니다. */
        bool saveGraphData();
        /** @brief 주어진 이름의 노드를 추가합니다. */
        void addNamedNode( const utf8* pName );
        /** @brief 미리보기 플레이어에 현재 그래프를 넣습니다. */
        void syncPreviewGraph();
        /** @brief 미리보기 재생을 한 틱 진행합니다. */
        void tickPreview( float32 deltaSeconds );

    private:
        AnimationGraphPlayer _previewPlayer;
        vector<AnimClip>     _listPreviewClip;
    };
} // namespace sw::editor
