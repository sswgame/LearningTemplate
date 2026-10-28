/**
 * @file MeshPreviewPanel.h
 * @brief 메시 에셋(`*.mesh`)을 미리보기 씬에서 돌려 보고 LOD 단계를 골라 견주는 창입니다(유니티 모델 임포터 미리보기의 LOD 보기, 언리얼 Static Mesh 에디터의 LOD 선택에 해당합니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"

namespace sw
{
    class MeshComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class MeshPreviewPanel
     * @brief 연 메시를 미리보기 씬의 오브젝트 하나(`MeshComponent`)로 보이고, 단계별 정점 수와 임계값 표에서 LOD 를 골라 그 단계만 그리게 합니다.
     * @details **보기 전용**이다. `.mesh` 는 모델 임포트의 산출물이라 이 창이 저장하지 않는다(원본을 다시 임포트하면 덮인다). Mesh 종류 중 `.mesh` 만 문서로 받는다
     *          (`acceptsDocumentPath` — 같은 종류에 모델 원본 `.fbx` · `.gltf` 도 있다). LOD 는 씬 단위로 고정한다(`MeshLODSystem::setForcedLOD`) — 화면 크기로 고르는 자동 선택은
     *          창이 "Auto" 일 때만 돈다.
     */
    class MeshPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "mesh_preview";

        MeshPreviewPanel();

        const utf8*        getPanelTitle() const override { return "Mesh Preview"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 미리보기 메시의 LOD 0 정점 수입니다. 아직 읽는 중이거나 읽지 못했거나 씬이 없으면 false 입니다(감시 값 `Editor.MeshPreviewVertexCount`). */
        [[nodiscard]] bool findPreviewVertexCount( uint32& outCount ) const;
        /** @brief 미리보기 메시가 지금 그리는 LOD 단계입니다. 없으면 false 입니다(감시 값 `Editor.MeshPreviewDrawnLOD`). */
        [[nodiscard]] bool findPreviewDrawnLOD( uint32& outLOD ) const;

    private:
        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 구경 바닥을 끕니다 — 메시 원점이 바닥에 겹쳐 가리지 않게 한다. */
        EditorPreviewEnvironment getPreviewEnvironment() const override;
        /** @brief 미리보기 씬에 메시 오브젝트 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 씬의 메시 컴포넌트입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        MeshComponent* findPreviewSubject() const;
        /** @brief 미리보기 메시가 지금 문서를 그리게 하고 읽힌 뒤 한 번 카메라를 맞춥니다. */
        void syncPreviewSubject();
        /** @brief 고른 LOD 를 미리보기 씬에 건다. */
        void applyLODChoice();
        void drawLODTable( const MeshComponent& subject );

        string _previewMeshPath; ///< 미리보기 메시에 건 경로(비면 다음 그리기가 다시 건다)
        string _status;
        int32  _lodChoice; ///< 고른 LOD 단계(-1 = Auto, 화면 크기로 고른다)
        bool   _bFramed;   ///< 읽힌 메시로 카메라를 한 번 맞췄다
    };
} // namespace sw::editor
