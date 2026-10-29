/**
 * @file VFXPreviewPanel.h
 * @brief 파티클 효과(`*.vfx.xml`)를 미리보기 씬에서 재생해 보는 창입니다(유니티 Particle Effect 미리보기, 언리얼 Niagara 에디터 뷰포트에 해당).
 * @details 방출기 표 · 효과 걸기는 `VFXPreviewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 미리보기 씬 · 뷰포트만 쥔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Panels/VFX/Model/VFXPreviewModel.h"

namespace sw::editor
{
    /**
     * @class VFXPreviewPanel
     * @brief 연 효과를 미리보기 씬의 오브젝트 하나(`VFXComponent`)로 재생하고, 방출기 목록과 살아 있는 입자 수를 보입니다.
     * @details **보기 전용**이다. 효과 파일은 데이터 종류(`vfx`)라 카탈로그 편집기가 고친다 — 같은 파일을 두 편집기가 쥐지 않게 이 창은 저장하지 않는다.
     *          카탈로그 편집기에서 저장한 뒤 Reload 로 다시 읽는다. Data 종류 중 `*.vfx.xml` 만 문서로 받는다(`acceptsDocumentPath`).
     */
    class VFXPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "vfx_preview";

        VFXPreviewPanel();

        const utf8* getPanelTitle() const override { return "VFX Preview"; }
        void        drawDocumentContent() override;
        /** @brief 보기 전용이라 저장하지 않습니다(효과는 카탈로그 편집기가 고친다). */
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /**
         * @brief 미리보기 효과가 이번 재생에서 만든 입자 수입니다(일정에서 오므로 CPU · GPU 방출기가 같다). 미리보기 씬이 없으면 false 입니다(감시 값 `Editor.VFXPreviewParticleCount`).
         * @details GPU 방출기의 살아 있는 수는 GPU 버퍼에만 있어 게임 스레드가 모른다 — 그린 것은 감시 값 `Editor.VFXPreviewSceneFrames` 와 캡처로 본다.
         */
        [[nodiscard]] bool findPreviewSpawnedParticleCount( uint64& outCount ) const;

    private:
        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬에 효과 오브젝트 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;

        VFXPreviewModel _model;
    };
} // namespace sw::editor
