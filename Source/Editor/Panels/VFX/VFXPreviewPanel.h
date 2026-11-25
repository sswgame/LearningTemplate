/**
 * @file VFXPreviewPanel.h
 * @brief 파티클 효과(`*.vfx.xml`)를 미리보기 씬에서 재생해 보는 창입니다(유니티 Particle Effect 미리보기, 언리얼 Niagara 에디터 뷰포트에 해당).
 * @details 방출기 표 · 효과 걸기는 `VFXPreviewViewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 미리보기 씬 · 뷰포트만 쥔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Widgets/PropertyGrid/EditorPropertyGrid.h"
#include "Editor/Panels/VFX/Model/VFXPreviewViewModel.h"

namespace sw::editor
{
    /**
     * @class VFXPreviewPanel
     * @brief 연 효과를 미리보기 씬의 오브젝트 하나(`VFXComponent`)로 재생하고, 방출기 목록과 살아 있는 입자 수를 보입니다.
     * @details 보기 전용이다. 효과 파일은 데이터 종류(`vfx`)라 카탈로그 편집기가 고친다 — 같은 파일을 두 편집기가 쥐지 않게 이 창은 저장하지 않는다.
     *          카탈로그 편집기(또는 바깥)에서 저장하면 파일 감시가 알리고 내용 해시가 바뀐 것을 본 이 창이 스스로 다시 읽어 다시 건다(`VFXPreviewViewModel::pollFileChanged`). Reload 는 손으로 다시 읽기다. Data 종류 중 `*.vfx.xml` 만 문서로 받는다(`canAcceptDocumentPath`).
     */
    class VFXPreviewPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "vfx_preview";

        VFXPreviewPanel();

        const utf8* getPanelTitle() const override { return "VFX Preview"; }
        void        drawDocumentContent() override;
        /** @brief 고친 효과 원문을 씁니다. 저장한 파일을 공유 캐시가 다시 읽고(`onDocumentSaved`) 미리보기가 다음 그리기에 다시 건다. */
        [[nodiscard]] bool saveDocument() override;
        /** @brief 고른 방출기 번호입니다. 없으면 -1 입니다(감시 값 `Editor.VFXSelectedEmitter`). */
        int32 getSelectedEmitter() const;
        /** @brief 방출기 수입니다(편집 중인 원문, 감시 값 `Editor.VFXEmitterCount`). */
        uint32 getEmitterCount() const { return static_cast<uint32>( _model.getEmitterRows().size() ); }

        /**
         * @brief 미리보기 효과가 이번 재생에서 만든 입자 수입니다(일정에서 오므로 CPU · GPU 방출기가 같다). 미리보기 씬이 없으면 false 입니다(감시 값 `Editor.VFXPreviewParticleCount`).
         * @details GPU 방출기의 살아 있는 수는 GPU 버퍼에만 있어 게임 스레드가 모른다 — 그린 것은 감시 값 `Editor.VFXPreviewSceneFrames` 와 캡처로 본다.
         */
        [[nodiscard]] bool findPreviewSpawnedParticleCount( uint64& outCount ) const;
        /** @brief 효과 파일이 바뀌어 스스로 다시 읽은 횟수입니다(감시 값 `Editor.VFXPreviewAutoReloadCount`). */
        uint32 getAutoReloadCount() const { return _autoReloadCount; }
        /** @brief 격리한 방출기 번호입니다(`VFXPreviewViewModel::kNoSolo` 면 없음 — 감시 값 `Editor.VFXPreviewSoloEmitter`). */
        int32 getSoloEmitter() const { return _model.getSoloEmitter(); }
        /** @brief 미리보기 효과에서 켜진 방출기 수입니다. 미리보기 씬이 없으면 false 입니다(감시 값 `Editor.VFXPreviewEnabledEmitterCount`). */
        [[nodiscard]] bool findEnabledEmitterCount( uint32& outCount ) const;
        /** @brief 캐릭터 타깃 몸이 미리보기 씬에 있으면 true 입니다(감시 값 `Editor.VFXPreviewHasTarget`). */
        [[nodiscard]] bool hasPreviewTarget() const { return _model.hasTarget( getPreviewScene() ); }

    private:
        bool                canAcceptDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _model.captureText(); }
        void                applyDocumentText( string_view text ) override { _model.applyText( text ); }
        /** @brief 방출기 편집 칸(고른 방출기의 프로퍼티 · 복사 · 빼기)을 그립니다. 고칠 수 없는 효과면 까닭을 보인다. */
        void drawEmitterEditor();
        void onEmitterPropertyEdited( const PropertyInfo& property );
        /** @brief 미리보기 씬에 효과 오브젝트 하나(`PreviewSubject`)를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 맞춤(F)과 창을 처음 열 때의 카메라가 담을 구입니다. */
        bool findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const override;

    private:
        VFXPreviewViewModel _model;
        EditorPropertyGrid  _grid;
        uint32              _autoReloadCount; ///< 파일이 바뀌어 다시 읽은 횟수
    };
} // namespace sw::editor
