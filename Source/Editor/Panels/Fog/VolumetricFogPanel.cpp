#include "pch.h"

#include "Editor/Panels/Fog/VolumetricFogPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Fog/Model/VolumetricFogStatus.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct VolumetricFogPanelInternal
        {
            static constexpr const utf8* kViewVariable  = "gv_fogDebugView";  ///< 안개 패스의 진단 보기(개발 도구 `RenderDiagnosticsSwitches.cpp` 가 진단 정책으로 넘긴다)
            static constexpr const utf8* kSliceVariable = "gv_fogDebugSlice"; ///< 진단 단면의 거리 조각

            /** @brief 전역 변수의 지금 글입니다. 등록되지 않았으면(Shipping 이 아닌데 없으면 결함) 빈 글입니다. */
            static string readVariable( const utf8* pName )
            {
                const GlobalVariableInfo* pInfo = editor::getSubsystem<GlobalVariableManager>().findVariable( pName );
                return pInfo != nullptr ? pInfo->getValueAsString() : string{};
            }

            static void writeVariable( const utf8* pName, string_view value )
            {
                if ( editor::getSubsystem<GlobalVariableManager>().setValueFromString( pName, value ) == false )
                    SW_LOG_WARNING_ID( "VolumetricFogPanel.VariableNotSet", "The fog debug variable '%#' did not take '%#'", pName, value );
            }

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
        : IEditorPanel( false )
    {
    }

    void VolumetricFogPanel::drawContent()
    {
        using Internal             = VolumetricFogPanelInternal;
        const FogVolumeState state = FrameFog::getLastMainVolumeState();
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
        using Internal                 = VolumetricFogPanelInternal;
        const string       current     = Internal::readVariable( Internal::kViewVariable );
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
                Internal::writeVariable( Internal::kViewVariable, pName );
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( arrMark[index] );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorVolumetricFog", "DebugViewTip" ) );
        int32 slice = 0;
        (void)StringUtil::parseInt( string_view( Internal::readVariable( Internal::kSliceVariable ) ), slice ); // 등록한 int 변수의 글이다
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorVolumetricFog", "Slice" ), SW_LOCKEY( "EditorVolumetricFog", "SliceTip" ) );
        if ( ImGui::SliderInt( "##fogSlice", &slice, 0, static_cast<int32>( SW_FOG_VOLUME_DIM_Z ) - 1 ) )
        {
            Internal::writeVariable( Internal::kSliceVariable, to_string( slice ) );
        }
        EditorSelfTestMarks::record( "volumetricFog.slice" );
    }

    void VolumetricFogPanel::drawFogObjectRow()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EditorVolumetricFog", "Settings" ) );
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pFog     = nullptr;
        if ( pManager != nullptr )
        {
            // 공유 잠금 안의 콜백이라 찾기만 한다(고르기는 잠금 밖에서).
            pManager->forEachComponentOfType<FogComponent>( [&pFog]( FogComponent* pComponent )
            {
                if ( pFog == nullptr )
                    pFog = pComponent->getOwner();
            } );
        }
        if ( pFog == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorVolumetricFog", "NoFogHint" ) );
            return;
        }
        ImGui::TextUnformatted( pFog->getName().c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorVolumetricFog", "SelectFog" ) ) );
        // 값은 인스펙터에서 고친다 — 씬의 `FogComponent` 가 정본이고 이 창은 사본을 두지 않는다.
        if ( ImGui::Button( SW_LOCKEY( "EditorVolumetricFog", "SelectFog" ) ) )
            editor::getMainSceneSelection().selectObject( pFog );
        EditorSelfTestMarks::record( "volumetricFog.selectFog" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorVolumetricFog", "SelectFogTip" ) );
    }
} // namespace sw::editor
