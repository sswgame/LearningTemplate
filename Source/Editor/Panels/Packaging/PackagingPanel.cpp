#include "pch.h"

#include "Editor/Panels/Packaging/PackagingPanel.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PackagingPanelInternal
        {
            /** @brief 이름 목록에서 하나를 고르는 콤보입니다. @p getName 이 순번의 이름을 돌려주고, 고르면 @p inoutIndex 를 바꿉니다. */
            template <typename GetName>
            static void drawOptionCombo( const utf8* pLabel, float32 width, const utf8* pPreview, uint32 count, GetName&& getName, uint32& inoutIndex )
            {
                ImGui::SetNextItemWidth( width );
                if ( EditorWidgets::beginCombo( pLabel, pPreview ) == false )
                    return;
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( ImGui::Selectable( getName( index ), index == inoutIndex ) )
                        inoutIndex = index;
                }
                ImGui::EndCombo();
            }

            static const PackagingPanel* findPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<PackagingPanel>() : nullptr;
            }

            [[nodiscard]] static bool readPackagingState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const PackagingPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getState() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( PackagingPanel, PackagingPanel::kPanelID, EditorPanelCategory::Tool, 2050 );
    SW_AUTOMATION_WALL_WATCH( editorPackagingState, "Editor.PackagingState", "Packaging window: 0 idle, 1 running, 2 succeeded, 3 failed",
                              &PackagingPanelInternal::readPackagingState );

    PackagingPanel::PackagingPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _model{}
    {
    }

    void PackagingPanel::shutdown()
    {
        _model.cancelAndWait();
    }

    void PackagingPanel::drawContent()
    {
        _model.update();
        const bool bBusy = _model.isBusy();

        drawOptionRows( bBusy );
        drawActionRow( bBusy );
        drawProgressBar();
        drawLogList();
    }

    void PackagingPanel::drawOptionRows( bool bBusy )
    {
        using Internal         = PackagingPanelInternal;
        const float32 dpiScale = EditorThemeUtil::getDPIScale();

        ImGui::BeginDisabled( bBusy );
        Internal::drawOptionCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Target" ), "Target" ), 120.0f * dpiScale, PackagingModel::getTargetName( _model.getTargetIndex() ), PackagingModel::getTargetCount(),
                                   []( uint32 index )
        { return PackagingModel::getTargetName( index ); }, _model.editTargetIndex() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Target" ) );
        ImGui::SameLine();
        const vector<string>& listGame     = _model.getGames();
        const utf8*           pGamePreview = _model.getGameIndex() < listGame.size() ? listGame[_model.getGameIndex()].c_str() : "(none)";
        Internal::drawOptionCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Game" ), "Game" ), 160.0f * dpiScale, pGamePreview, static_cast<uint32>( listGame.size() ),
                                   [&listGame]( uint32 index )
        { return listGame[index].c_str(); }, _model.editGameIndex() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Game" ) );
        ImGui::SameLine();
        Internal::drawOptionCombo( "RHI", 150.0f * dpiScale, PackagingModel::getBackendName( _model.getRHIIndex() ), PackagingModel::getBackendCount(),
                                   []( uint32 index )
        { return PackagingModel::getBackendName( index ); }, _model.editRHIIndex() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "RHIBackend" ) );

        EditorChrome::setNextItemWidthColumns( 43.0f );
        (void)EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "OutputFolder" ), "Output Folder" ), _model.editOutputFolder() ); // 바뀐 값은 다음 Package 가 쓴다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "OutputFolder" ) );

        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "SkipBuild" ), &_model.editSkipBuild() ); // 바뀐 값은 다음 Package 가 쓴다
        EditorSelfTestMarks::record( "packaging.skipBuild" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "SkipBuild" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "SkipCook" ) ) );
        (void)ImGui::Checkbox( SW_LOCKEY( "EdLabel", "SkipCook" ), &_model.editSkipCook() );
        EditorSelfTestMarks::record( "packaging.skipCook" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "SkipCook" ) );
        ImGui::EndDisabled();
    }

    void PackagingPanel::drawActionRow( bool bBusy )
    {
        ImGui::BeginDisabled( bBusy );
        const bool bPackage = ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "BuildAll" ), SW_LOCKEY( "EdButton", "Package" ), "Package" ) );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "packaging.package" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Package" ) );
        if ( bPackage )
            (void)_model.startPackaging(); // 이미 돌면 단추가 꺼져 있다
        ImGui::SameLine();
        const PackagingProgress& progress   = _model.getProgress();
        const bool               bHasFolder = progress._state == PackagingState::Succeeded && progress._outputFolder.empty() == false;
        ImGui::BeginDisabled( bHasFolder == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "FolderOpen" ), SW_LOCKEY( "EdButton", "OpenFolder" ), "Open Folder" ) ) )
            PackagingModel::showInFileExplorer( progress._outputFolder );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "OpenFolder" ) );
    }

    void PackagingPanel::drawProgressBar()
    {
        // 진행 — 단계 줄은 끝난 뒤 한꺼번에 오므로 도는 동안은 움직이는 막대다.
        const PackagingProgress&                progress = _model.getProgress();
        fixed_string<constant::buffer::kMax256> overlay;
        float32                                 fraction = progress.computeFraction();
        switch ( progress._state )
        {
            case PackagingState::Idle:
            {
                formatstring( overlay.data(), overlay.capacity(), "Idle" );
                break;
            }
            case PackagingState::Running:
            {
                fraction = static_cast<float32>( -ImGui::GetTime() );
                formatstring( overlay.data(), overlay.capacity(), "Packaging..." );
                break;
            }
            case PackagingState::Succeeded:
            {
                formatstring( overlay.data(), overlay.capacity(), "Done - %# MB", progress._byteCount / constant::memory::kBytesPerMegabyte );
                break;
            }
            case PackagingState::Failed:
            {
                formatstring( overlay.data(), overlay.capacity(), "Failed at %#", progress._stepName.c_str() );
                break;
            }
        }
        ImGui::ProgressBar( fraction, ImVec2( -1.0f, 0.0f ), overlay.c_str() );
        if ( progress._state == PackagingState::Failed && progress._failure.empty() == false )
            EditorThemeUtil::textError( progress._failure.c_str() );
        else if ( progress._state == PackagingState::Succeeded )
            ImGui::TextDisabled( "%s", progress._outputFolder.c_str() );
    }

    void PackagingPanel::drawLogList()
    {
        ImGui::Separator();
        if ( ImGui::BeginChild( "##packaging_log", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders ) )
        {
            const vector<string>& listLine  = _model.getLines();
            const size_t          firstLine = listLine.size() > PackagingModel::kVisibleLineCount ? listLine.size() - PackagingModel::kVisibleLineCount : 0;
            for ( size_t lineIndex = firstLine; lineIndex < listLine.size(); ++lineIndex )
            {
                const string& line = listLine[lineIndex];
                if ( PackagingModel::isErrorLine( line ) )
                    EditorThemeUtil::textError( line.c_str() );
                else
                    ImGui::TextUnformatted( line.c_str() );
            }
        }
        ImGui::EndChild();
    }
} // namespace sw::editor
