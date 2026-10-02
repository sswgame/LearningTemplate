#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

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

    private:
        shared_ptr<Material>                  _material; ///< 편집 사본. Material 은 create() 로만 만들 수 있습니다
        fixed_string<constant::kMaxBuffer128> _name;
        fixed_string<constant::kMaxBuffer256> _shaderPath;
        string                                _status;
    };
} // namespace sw::editor
