#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

namespace sw
{
    class Material;
    class MeshComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 마스터 머티리얼 XML을 편집합니다.
     * @details 미리보기는 둘이다 — 렌더러가 그리는 미리보기 씬의 구(`EditorPreviewPanel`, 메인 씬과 섞이지 않는다)와,
     *          값으로 CPU 에서 셰이딩한 작은 구(`MaterialPreviewShading` — 렌더 타깃이 없을 때의 폴백이자 탐침 `Editor.MaterialPreviewRedMinusBlue` 의 출처).
     */
    class MaterialPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "material";

        MaterialPanel();

        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

    private:
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        void                syncNameBuffers();
        void                applyLivePreview();
        /** @brief 머티리얼 값으로 셰이딩한 구 미리보기를 그립니다(`MaterialPreviewShading`). */
        void drawPreview();
        /** @brief 창이 접히거나 탭이 가려져 내용을 그리지 않은 프레임입니다 — 지난 미리보기 값은 더 이상 지금 문서의 것이 아니다. */
        void onPanelCollapsed() override { _bPreviewDrawn = false; }
        /** @brief 미리보기 씬에 구 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 씬의 구입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        MeshComponent* findPreviewSubject() const;
        /** @brief 미리보기 구가 지금 문서의 머티리얼을 쓰게 합니다(경로가 바뀌었을 때만). */
        void syncPreviewSubjectMaterial();

    public:
        /** @brief 지난 그리기의 미리보기 평균 (R - B)(0..255)입니다. 지난 프레임에 그리지 않았으면 false 입니다(탐침 `Editor.MaterialPreviewRedMinusBlue`). */
        bool findPreviewRedMinusBlue( float32& outValue ) const;
        /** @brief 지금 연 문서가 @p assetPath(리소스 id)이고 읽기에 성공했으면 true 입니다(탐침 `Editor.MaterialDocumentIsProbeAsset`). 읽지 못했으면 편집 사본은 앞 문서의 값이다. */
        bool hasDocument( string_view assetPath ) const { return getLoadedAssetPath() == assetPath && isDocumentLoaded() && isDocumentLoadFailed() == false; }
        /** @brief 미저장 편집 때문에 "그래도 바꿀까요?" 대답을 기다리는 중이면 true 입니다(탐침 `Editor.MaterialSwitchPending`). */
        bool isWaitingForSwitchAnswer() const { return isSwitchConfirmPending(); }

    private:
        shared_ptr<Material>                  _material; ///< 편집 사본. Material 은 create() 로만 만들 수 있습니다
        fixed_string<constant::kMaxBuffer128> _name;
        fixed_string<constant::kMaxBuffer256> _shaderPath;
        string                                _status;
        string                                _previewMaterialPath; ///< 미리보기 구에 건 머티리얼 경로
        float32                               _previewRedMinusBlue; ///< 지난 그리기의 미리보기 평균 (R - B)
        bool                                  _bPreviewDrawn;       ///< 지난 그리기에서 미리보기를 그렸다
    };
} // namespace sw::editor
