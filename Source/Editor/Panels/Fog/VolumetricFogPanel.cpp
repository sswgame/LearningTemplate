/**
 * @file VolumetricFogPanel.cpp
 * @brief 볼륨 안개 창입니다 — 주 화면 볼륨 안개의 상태(켜졌거나 왜 꺼졌는지), 같은 프레임의 볼륨 · 평범한 안개 분할 비교, froxel 단면(산란 · 투과율)과 거리 조각, 씬의 안개 고르기.
 * @details 저작 값은 씬의 `FogComponent` 가 정본이다 — 이 창은 새 형식 · 사본을 두지 않고 안개 오브젝트를 골라 인스펙터가 고치게 한다(값을 바꾸면 같은 프레임 조건으로 곧바로 다시 그려진다).
 *          진단 보기는 개발 빌드 변수(`gv_fogDebugView` · `gv_fogDebugSlice`)라 저장하지 않는다. 볼륨이 꺼진 화면을 볼륨 안개로 오인하지 않게 이유를 문장으로 보인다
 *          (언리얼 Volumetric Fog 의 Buffer Visualization · 경고 줄과 같은 자리). 상태와 문장 고르기는 `VolumetricFogStatus`(ImGui 없음)가, 변수 · 씬 읽기와 명령은 `VolumetricFogViewModel` 이 한다(MVVM 계약).
 */
#include "pch.h"

#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Fog/Model/VolumetricFogStatus.h"
#include "Editor/Panels/Fog/Model/VolumetricFogViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/EnumName.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class VolumetricFogPanel
     * @brief 볼륨 안개 진단 창입니다. 파일 머리말 참고.
     */
    class VolumetricFogPanel : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "volumetric_fog";
        /** @brief 처음 열 때의 크기(글자 수)입니다. */
        static constexpr EditorTextExtent kInitialPanelSize = EditorTextExtent::ofText( 60.0f, 20.0f );

        VolumetricFogPanel();

        const utf8*      getPanelTitle() const override { return "Volumetric Fog"; }
        EditorTextExtent getInitialPanelSize() const override { return kInitialPanelSize; }
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

    namespace
    {
        struct VolumetricFogPanelInternal
        {
            static Color getSeverityColor( VolumetricFogSeverity severity )
            {
                switch ( severity )
                {
                    case VolumetricFogSeverity::Neutral:
                        return SW_THEME_COLOR( "Base", "TextMuted" );
                    case VolumetricFogSeverity::Active:
                        return SW_THEME_COLOR( "Signal", "Success" );
                    case VolumetricFogSeverity::Problem:
                        return SW_THEME_COLOR( "Signal", "Warning" );
                }
                return SW_THEME_COLOR( "Base", "Text" );
            }
        };
    } // namespace

    SW_EDITOR_PANEL( VolumetricFogPanel, VolumetricFogPanel::kPanelID, EditorPanelCategory::Tool, 910 );

    VolumetricFogPanel::VolumetricFogPanel()
        : EditorPanel( false )
        , _viewModel{}
        , _viewState{}
    {
    }

    void VolumetricFogPanel::drawContent()
    {
        using Internal = VolumetricFogPanelInternal;
        gatherViewState( _viewModel, _viewState );
        const FogVolumeState state = _viewState._volumeState;
        // 상태는 문장으로 — 색만으로 알리지 않는다. 켜짐은 Success, 못 켬은 Warning, 고른 꺼짐은 흐린 글이다.
        ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( Internal::getSeverityColor( VolumetricFogStatus::getSeverity( state ) ) ) );
        ImGui::TextWrapped( "%s", VolumetricFogStatus::describe( state ) );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::record( "volumetricFog.state" );
        drawDebugViewRow( VolumetricFogStatus::canShowVolumeViews( state ) );
        drawFogObjectRow();
        // 진단 보기는 뷰포트 그림을 바꾸므로 에디터가 쉬지 않고 다시 그리게 한다(상태도 프레임마다 바뀔 수 있다).
        requestAnimationFrame();
    }

    void VolumetricFogPanel::drawDebugViewRow( bool bVolumeActive )
    {
        const string&      current     = _viewState._debugView;
        const FogDebugView arrView[4]  = { FogDebugView::None, FogDebugView::Compare, FogDebugView::Scattering, FogDebugView::Transmittance };
        const utf8* const  arrLabel[4] = { SW_LOCKEY( "EditorVolumetricFog", "ViewNormal" ), SW_LOCKEY( "EditorVolumetricFog", "ViewCompare" ),
                                           SW_LOCKEY( "EditorVolumetricFog", "ViewScattering" ), SW_LOCKEY( "EditorVolumetricFog", "ViewTransmittance" ) };
        const utf8* const  arrMark[4]  = { "volumetricFog.view.None", "volumetricFog.view.Compare", "volumetricFog.view.Scattering", "volumetricFog.view.Transmittance" };
        ImGui::SeparatorText( SW_LOCKEY( "EditorVolumetricFog", "DebugView" ) );
        for ( uint32 index = 0; index < 4; ++index )
        {
            if ( index > 0 )
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( arrLabel[index] ) );
            const utf8* pName   = getEnumName( arrView[index] );
            const bool  bActive = current == pName;
            // 볼륨이 없으면 비교 · 단면은 그릴 것이 없다 — 단추를 끄고 까닭은 위 문장이 말한다.
            ImGui::BeginDisabled( index > 0 && bVolumeActive == false );
            if ( EditorWidgets::drawToggleButton( arrLabel[index], bActive ) && bActive == false )
                _viewModel.setDebugView( arrView[index] );
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( arrMark[index] );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorVolumetricFog", "DebugViewTip" ) );
        int32 slice = _viewState._slice;
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorVolumetricFog", "Slice" ), SW_LOCKEY( "EditorVolumetricFog", "SliceTip" ) );
        if ( EditorWidgets::sliderInt( SW_WIDGET_ID( "##fogSlice" ), &slice, 0, static_cast<int32>( SW_FOG_VOLUME_DIM_Z ) - 1 ) )
            _viewModel.setSlice( slice );
        EditorSelfTestMarks::record( "volumetricFog.slice" );
    }

    void VolumetricFogPanel::drawFogObjectRow()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorVolumetricFog", "Settings" ) );
        if ( _viewState._fogObjectID == 0 )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorVolumetricFog", "NoFogHint" ) );
            return;
        }
        ImGui::TextUnformatted( _viewState._fogObjectName.c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorVolumetricFog", "SelectFog" ) ) );
        // 값은 인스펙터에서 고친다 — 씬의 `FogComponent` 가 정본이고 이 창은 사본을 두지 않는다.
        if ( ImGui::Button( SW_LOCKEY( "EditorVolumetricFog", "SelectFog" ) ) )
            _viewModel.selectFogObject( _viewState._fogObjectID );
        EditorSelfTestMarks::record( "volumetricFog.selectFog" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorVolumetricFog", "SelectFogTip" ) );
    }
} // namespace sw::editor
