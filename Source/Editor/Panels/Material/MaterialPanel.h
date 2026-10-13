#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"

namespace sw
{
    class Material;
} // namespace sw

namespace sw::editor
{
    /** @brief 마스터 머티리얼 XML을 편집합니다. */
    class MaterialPanel : public EditorDocumentPanel
    {
    public:
        MaterialPanel();

        void               drawContent() override;
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
        float32                               _previewRedMinusBlue; ///< 지난 그리기의 미리보기 평균 (R - B)
        bool                                  _bPreviewDrawn;       ///< 지난 그리기에서 미리보기를 그렸다
    };
} // namespace sw::editor
