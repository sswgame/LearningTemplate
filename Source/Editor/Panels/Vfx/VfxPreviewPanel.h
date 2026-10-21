/**
 * @file VfxPreviewPanel.h
 * @brief 파티클 효과(`*.vfx.xml`)를 미리보기 씬에서 재생해 보는 창입니다(유니티 Particle Effect 미리보기, 언리얼 Niagara 에디터 뷰포트에 해당).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

namespace sw
{
    class VfxComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class VfxPreviewPanel
     * @brief 연 효과를 미리보기 씬의 오브젝트 하나(`VfxComponent`)로 재생하고, 방출기 목록과 살아 있는 입자 수를 보입니다.
     * @details **보기 전용**이다. 효과 파일은 데이터 종류(`vfx`)라 카탈로그 편집기가 고친다 — 같은 파일을 두 편집기가 쥐지 않게 이 창은 저장하지 않는다.
     *          카탈로그 편집기에서 저장한 뒤 Reload 로 다시 읽는다. Data 종류 중 `*.vfx.xml` 만 문서로 받는다(`acceptsDocumentPath`).
     */
    class VfxPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "vfx_preview";

        VfxPreviewPanel();

        const utf8* getPanelTitle() const override { return "VFX Preview"; }
        void        drawDocumentContent() override;
        /** @brief 보기 전용이라 저장하지 않습니다(효과는 카탈로그 편집기가 고친다). */
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 지금 연 문서가 @p assetPath(리소스 id)이고 읽기에 성공했으면 true 입니다(탐침 `Editor.VfxPreviewDocumentIsProbeAsset`). */
        bool hasDocument( string_view assetPath ) const { return getLoadedAssetPath() == assetPath && isDocumentLoaded() && isDocumentLoadFailed() == false; }
        /**
         * @brief 미리보기 효과가 이번 재생에서 만든 입자 수입니다(일정에서 오므로 CPU · GPU 방출기가 같다). 미리보기 씬이 없으면 false 입니다(탐침 `Editor.VfxPreviewParticleCount`).
         * @details GPU 방출기의 살아 있는 수는 GPU 버퍼에만 있어 게임 스레드가 모른다 — 그린 것은 탐침 `Editor.VfxPreviewSceneFrames` 와 캡처로 본다.
         */
        bool findPreviewSpawnedParticleCount( uint64& outCount ) const;

    private:
        /** @brief 방출기 하나의 요약(목록 표시용)입니다. */
        struct EmitterRow
        {
            string _name{};
            string _texturePath{};
            uint32 _maxParticles{ 0 };
            bool   _bAdditive{ false };
            bool   _bLooping{ false };
        };

        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬에 효과 오브젝트 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 씬의 효과입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        VfxComponent* findPreviewSubject() const;
        /** @brief 미리보기 효과가 지금 문서를 재생하게 합니다(문서가 바뀌었거나 다시 읽었을 때만). */
        void syncPreviewSubjectEffect();

        vector<EmitterRow> _listEmitterRow;
        string             _previewEffectPath; ///< 미리보기 효과에 건 효과 경로(비면 다음 그리기가 다시 건다)
        string             _status;
    };
} // namespace sw::editor
