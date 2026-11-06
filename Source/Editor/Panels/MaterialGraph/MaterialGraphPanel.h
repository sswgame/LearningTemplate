/**
 * @file MaterialGraphPanel.h
 * @brief 머티리얼 그래프 창입니다(Window > Material Graph). `*.matgraph.xml` 문서를 캔버스 · 인스펙터로 고치고 미리보기 구에 바로 보이며, 컴파일이 통과하면 저장합니다.
 * @details 언리얼 Material Editor · 유니티 Shader Graph · Godot Visual Shader 의 자리입니다(비교는 기억에 의존). 상태 · 편집 명령은 `MaterialGraphModel`(Model/, ImGui 없음)이 든다.
 *          저장은 그래프 파일만 쓰고, 생성물은 저장 뒤 핫 리로드 임포터가 만든다(`MaterialGraphAssetTypeActions.cpp`) — 바깥 편집기 · git 으로 그래프가 바뀐 때와 같은 길이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Panels/MaterialGraph/Model/MaterialGraphModel.h"

namespace sw::editor
{
    /**
     * @class MaterialGraphPanel
     * @brief 왼쪽 캔버스(오른쪽 클릭으로 노드 넣기, Delete 로 지우기)와 오른쪽 미리보기 · 값 · 문제 목록입니다.
     */
    class MaterialGraphPanel final : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "materialgraph";

        MaterialGraphPanel();

        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

    protected:
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _model.captureText(); }
        void                applyDocumentText( string_view text ) override;
        void                populatePreviewScene( Scene& scene ) override;
        bool                findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const override;

    private:
        void drawToolbar();
        void drawCanvas( float32 canvasWidth );
        void drawCanvasNode( uint32 nodeIndex );
        /** @brief 지운 링크 · 노드를 모두 모은 뒤 모델이 지우게 합니다. */
        void applyCanvasDeletions();
        void drawSidePanel();
        /** @brief 고른 노드의 설정과 잇지 않은 핀의 값입니다. */
        void drawInspector();
        /** @brief 값 한 줄입니다. 형식에 맞는 위젯을 그리고 고쳤으면 모델에 넘깁니다. */
        void drawValueRow( uint32 nodeIndex, const utf8* pName, MaterialGraphValueType type, string_view defaultText, bool bColor );
        void drawIssues();
        /** @brief 캔버스 프레임워크가 묻는 핀 정보입니다. float 출력은 어느 입력으로든 넓어져 와일드카드입니다. */
        [[nodiscard]] bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo );

    private:
        MaterialGraphModel                   _model;
        EditorNodeGraph                      _nodeGraph;
        vector<const MaterialGraphNodeKind*> _listKind;   ///< 등록표의 종류(노드 메뉴). 번호가 메뉴의 `_kindID` 다
        vector<EditorGraphNodeKind>          _listMenu;   ///< 노드 메뉴
        vector<int32>                        _listLinkID; ///< 이번 프레임에 캔버스가 지운 링크 id
        vector<int32>                        _listNodeID; ///< 이번 프레임에 캔버스가 지운 노드 id
    };
} // namespace sw::editor
