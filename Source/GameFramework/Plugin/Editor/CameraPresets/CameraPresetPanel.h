/**
 * @file CameraPresetPanel.h
 * @brief 카메라 프리셋 파일(`*.cameras.xml`)의 프리셋을 미리보기 씬에서 보는 창입니다(Cinemachine 의 프리셋 미리보기, 언리얼 카메라 리그 미리보기에 해당).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"

#include "GameFramework/Base/Character/Camera/CameraPreset.h"

namespace sw::editor
{
    /**
     * @class CameraPresetPanel
     * @brief 연 프리셋 파일의 프리셋 하나를 골라, 미리보기 씬의 대상(원점 위 캡슐)에 대해 그 프리셋이 놓는 카메라를 자유 카메라로 보입니다.
     * @details **보기 전용**이다. 프리셋 파일은 데이터 종류(`camerapresets`)라 카탈로그 편집기가 고친다 — 같은 파일을 두 편집기가 쥐지 않게 이 창은 저장하지 않는다.
     *          프리셋은 `evaluatePreset`(상태 없는 순수 계산)으로 푼다. 입력 · 암 · 프레이밍 · 흔들림 상태는 없어 Orbit 은 프리셋의 요 · 피치 그대로,
     *          Follow · FirstPerson · ThirdPerson 은 "대상 요" 슬라이더가 정한 방향의 대상에서 본 자리다. 직교 프리셋은 뷰포트가 원근만 그려 시야각으로 대신 보인다.
     *          뷰포트 카메라는 프리셋을 고르거나 대상 요를 바꿀 때만 놓는다 — 그 뒤 끌기 · 휠은 자유 카메라로 둘러본다.
     */
    class CameraPresetPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "camerapreset_preview";

        CameraPresetPanel();

        const utf8* getPanelTitle() const override { return "Camera Presets"; }
        void        drawDocumentContent() override;
        /** @brief 보기 전용이라 저장하지 않습니다(프리셋은 카탈로그 편집기가 고친다). */
        [[nodiscard]] bool saveDocument() override { return false; }
        bool               canSaveDocument() const override { return false; }

        /** @brief 지금 고른 프리셋 번호입니다(감시 값 `Editor.CameraPresetSelected`). 없으면 -1 입니다. */
        int32 getSelectedIndex() const { return _selectedIndex; }
        /** @brief 문서의 프리셋 수입니다. */
        uint32 getPresetCount() const { return static_cast<uint32>( _catalog.getCount() ); }

    private:
        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬에 대상(캡슐) 하나를 스폰합니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 고른 프리셋을 풀어 뷰포트 카메라를 그 자리에 놓습니다. */
        void applySelectedPresetToViewport();

        CameraPresetCatalog _catalog;
        int32               _selectedIndex;
        float32             _targetYawDegrees; ///< Follow · FirstPerson · ThirdPerson 이 따르는 대상 요(도)
        bool                _bViewportDirty;   ///< 다음 그리기에서 뷰포트 카메라를 프리셋 자리에 놓는다
        string              _status;
    };
} // namespace sw::editor
