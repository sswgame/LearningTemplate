/**
 * @file VolumetricFogPanel.h
 * @brief 볼륨 안개 창입니다 — 주 화면 볼륨 안개의 상태(켜졌거나 왜 꺼졌는지), 같은 프레임의 볼륨 · 평범한 안개 분할 비교, froxel 단면(산란 · 투과율)과 거리 조각, 씬의 안개 고르기.
 * @details 저작 값은 씬의 `FogComponent` 가 정본이다 — 이 창은 새 형식 · 사본을 두지 않고 안개 오브젝트를 골라 인스펙터가 고치게 한다(값을 바꾸면 같은 프레임 조건으로 곧바로 다시 그려진다).
 *          진단 보기는 개발 빌드 변수(`gv_fogDebugView` · `gv_fogDebugSlice`)라 저장하지 않는다. 볼륨이 꺼진 화면을 볼륨 안개로 오인하지 않게 이유를 문장으로 보인다
 *          (언리얼 Volumetric Fog 의 Buffer Visualization · 경고 줄과 같은 자리). 상태와 문장 고르기는 `VolumetricFogStatus`(ImGui 없음)가, 변수 · 씬 읽기와 명령은 `VolumetricFogViewModel` 이 한다(MVVM 계약).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Fog/Model/VolumetricFogViewModel.h"

namespace sw::editor
{
    /**
     * @class VolumetricFogPanel
     * @brief 볼륨 안개 진단 창입니다. 파일 머리말 참고.
     */
    class VolumetricFogPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "volumetric_fog";

        VolumetricFogPanel();

        const utf8*      getPanelTitle() const override { return "Volumetric Fog"; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 60.0f, 20.0f ); }
        void             drawContent() override;

    private:
        /** @brief 진단 보기 단추 줄과 단면 조각 슬라이더입니다. 볼륨이 꺼졌으면 단면 단추는 끈다(읽을 볼륨이 없다). */
        void drawDebugViewRow( bool bVolumeActive );
        /** @brief 씬의 안개 오브젝트를 골라 인스펙터로 넘기는 줄입니다. 없으면 무엇을 더할지 한 문장으로 알립니다. */
        void drawFogObjectRow();

    private:
        VolumetricFogViewModel _viewModel;
        VolumetricFogViewState _viewState; ///< 프레임마다 `gather` 가 채운다
    };
} // namespace sw::editor
