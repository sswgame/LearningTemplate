#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Graphics/Material/Material.h"

namespace sw::editor
{
    /** @brief 마스터 머티리얼 XML을 편집합니다. */
    class MaterialPanel : public EditorDocumentPanel
    {
    public:
        MaterialPanel();

        void drawContent() override;
        bool saveDocument() override;

    private:
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        void   loadFromFocusedPath();
        void   syncNameBuffers();
        void   applyLivePreview();

    private:
        Material                              _material;
        fixed_string<constant::kMaxBuffer128> _name;
        fixed_string<constant::kMaxBuffer256> _shaderPath;
        string                                _status;
    };
} // namespace sw::editor
