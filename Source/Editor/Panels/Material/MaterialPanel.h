#pragma once
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Panels/Material/Model/MaterialModel.h"

namespace sw::editor
{
    /**
     * @brief 마스터 머티리얼 XML을 편집합니다.
     * @details 미리보기는 둘이다 — 렌더러가 그리는 미리보기 씬의 구(`EditorPreviewPanel`, 메인 씬과 섞이지 않는다)와,
     *          값으로 CPU 에서 셰이딩한 작은 구(`MaterialPreviewShading` — 렌더 타깃이 없을 때의 폴백이자 감시 값 `Editor.MaterialPreviewRedMinusBlue` 의 출처).
     *          편집 사본 · 입력란 · 읽기 · 쓰기 · 미리보기 구 걸기는 `MaterialModel`(Model/, ImGui 없음)이 들고, 이 창은 위젯 · CPU 구 그리기와 문서 dirty · 되돌리기 기록만 맡는다.
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
        /** @brief 편집 사본을 선택 오브젝트와 캐시 사본에 바로 보입니다. */
        void applyLivePreview();
        /** @brief 머티리얼 값으로 셰이딩한 구 미리보기를 그립니다(`MaterialPreviewShading`). */
        void drawPreview();
        /** @brief 창이 접히거나 탭이 가려져 내용을 그리지 않은 프레임입니다 — 지난 미리보기 값은 더 이상 지금 문서의 것이 아니다. */
        void onPanelCollapsed() override { _model.beginFrame(); }
        /** @brief 미리보기 씬에 구 하나(`PreviewSubject`)를 스폰하고 카메라를 맞춥니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 구가 지금 문서의 머티리얼을 쓰게 합니다(경로가 바뀌었을 때만). */
        void syncPreviewSubjectMaterial();

    public:
        /** @brief 지난 그리기의 미리보기 평균 (R - B)(0..255)입니다. 지난 프레임에 그리지 않았으면 false 입니다(감시 값 `Editor.MaterialPreviewRedMinusBlue`). */
        [[nodiscard]] bool findPreviewRedMinusBlue( float32& outValue ) const { return _model.findPreviewRedMinusBlue( outValue ); }
        /** @brief 미저장 편집 때문에 "그래도 바꿀까요?" 대답을 기다리는 중이면 true 입니다(감시 값 `Editor.MaterialSwitchPending`). */
        bool isWaitingForSwitchAnswer() const { return isSwitchConfirmPending(); }

    private:
        MaterialModel _model;
    };
} // namespace sw::editor
