#include "pch.h"

#include "Editor/Panels/Packaging/PackagingPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Localization/LocText.h"

#include "sw/config/CookContract.gen.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "PackagingPanel" );

    namespace
    {
        struct PackagingPanelInternal
        {
            /** @brief 진입점(저장소 루트 기준)입니다. */
            static constexpr const utf8* kScriptRelativePath = "Scripts/package/Pack.py";
            /** @brief 진입점이 받는 타깃 이름입니다(`--target`). */
            static constexpr const utf8* kArrTarget[] = { "Client", "Server" };
            /** @brief 쿠킹 테이블의 백엔드 이름입니다(`--rhi` · `SW_SHIPPING_RHI_BACKEND`). 맨 앞은 "프리셋 기본값" 이다. */
#define SW_PACKAGING_BACKEND_NAME( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) #Backend,
            static constexpr const utf8* kArrBackend[] = { "(preset default)", SW_RHI_BACKEND_TABLE( SW_PACKAGING_BACKEND_NAME ) };
#undef SW_PACKAGING_BACKEND_NAME
            /** @brief 로그가 보이는 최대 줄 수입니다. */
            static constexpr size_t kVisibleLineCount = 400;

            /** @brief 이름 목록에서 하나를 고르는 콤보입니다. @p getName 이 순번의 이름을 돌려주고, 고르면 @p inoutIndex 를 바꿉니다. */
            template <typename GetName>
            static void drawOptionCombo( const utf8* pLabel, float32 width, const utf8* pPreview, uint32 count, GetName&& getName, uint32& inoutIndex )
            {
                ImGui::SetNextItemWidth( width );
                if ( ImGui::BeginCombo( pLabel, pPreview ) == false )
                    return;
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( ImGui::Selectable( getName( index ), index == inoutIndex ) )
                        inoutIndex = index;
                }
                ImGui::EndCombo();
            }

            /** @brief 오류처럼 보이는 줄입니다(빨강). */
            static bool isErrorLine( string_view line )
            {
                return StringUtil::contains( line, "FAILED", true ) || StringUtil::contains( line, "error", true );
            }

            static const PackagingPanel* findPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getSubsystem<EditorPanelManager>().findPanel<PackagingPanel>();
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
    SW_AUTOMATION_WATCH( editorPackagingState, "Editor.PackagingState", "Packaging window: 0 idle, 1 running, 2 succeeded, 3 failed",
                         &PackagingPanelInternal::readPackagingState );

    PackagingPanel::PackagingPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _pJob{ make_unique<EditorExternalToolJob>() }
        , _listGame{}
        , _listLine{}
        , _outputFolder{} // 비면 진입점의 기본 산출 폴더(`Pack.py --output` 의 기본값이 원본이다)
        , _progress{}
        , _gameIndex{ 0 }
        , _targetIndex{ 0 }
        , _rhiIndex{ 0 }
        , _bSkipBuild{ SW_FALSE }
        , _bSkipCook{ SW_FALSE }
        , _bGamesLoaded{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    PackagingPanel::~PackagingPanel()
    {
        if ( _pJob != nullptr )
            _pJob->cancelAndWait();
    }

    void PackagingPanel::shutdown( IRHIDevice* /*pDevice*/ )
    {
        _pJob->cancelAndWait();
    }

    void PackagingPanel::refreshGames()
    {
        _listGame.clear();
        _gameIndex = 0;
        vector<string> listFile;
        (void)FileUtil::collectFiles( FileUtil::joinPath( EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() ), "Config/Game" ), "", listFile, false ); // 폴더가 없으면 게임이 없다
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, ".json", true ) == false )
                continue;
            string     text;
            const bool bGameUsesPackRoot = FileUtil::readTextFile( filePath, text ) && packRoot.empty() == false && StringUtil::contains( text, "\"" + packRoot + "\"", true );
            if ( bGameUsesPackRoot )
                _gameIndex = static_cast<uint32>( _listGame.size() );
            _listGame.push_back( FileUtil::removeExtension( FileUtil::getFileNamePart( filePath ) ) );
        }
        _bGamesLoaded = SW_TRUE;
    }

    ProcessCommand PackagingPanel::makeCommand() const
    {
        using Internal               = PackagingPanelInternal;
        const string    projectRoot  = EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() );
        ProcessCommand  command      = EditorUtil::makePythonScriptCommand( FileUtil::joinPath( projectRoot, Internal::kScriptRelativePath ) );
        vector<string>& listArgument = command._listArgument;
        listArgument.insert( listArgument.end(), { "--target", Internal::kArrTarget[_targetIndex] } );
        if ( _gameIndex < _listGame.size() )
            listArgument.insert( listArgument.end(), { "--game", _listGame[_gameIndex] } );
        if ( _rhiIndex > 0 )
            listArgument.insert( listArgument.end(), { "--rhi", Internal::kArrBackend[_rhiIndex] } );
        if ( _outputFolder.empty() == false )
            listArgument.insert( listArgument.end(), { "--output", _outputFolder } );
        if ( _bSkipBuild == SW_TRUE )
        {
            // 빌드를 건너뛰면 이 에디터의 Bin 을 스테이징한다(그 타깃의 Shipping 빌드가 없어도 흐름을 볼 수 있다).
            listArgument.insert( listArgument.end(), { "--skip-build", "--bin-dir", FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) } );
        }
        if ( _bSkipCook == SW_TRUE )
            listArgument.push_back( "--skip-cook" );
        return command;
    }

    bool PackagingPanel::startPackaging()
    {
        if ( _pJob->request( makeCommand(), EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() ) ) == false )
            return false;
        _listLine.clear();
        _progress        = PackagingProgress{};
        _progress._state = PackagingState::Running;
        SW_LOG_INFO( "Packaging started: %#", makeCommand().toCommandLine() );
        return true;
    }

    void PackagingPanel::collectResult()
    {
        EditorExternalToolResult result{};
        if ( _pJob->take( result ) != EditorTaskPoll::Ready )
            return;
        _listLine             = std::move( result._listLine );
        _progress             = PackagingProgressParser::parseOutput( _listLine, result._bLaunched ? result._exitCode : -1 );
        const bool bSucceeded = _progress._state == PackagingState::Succeeded;
        if ( bSucceeded )
            SW_LOG_INFO( "Packaging done: %# (%# bytes)", _progress._outputFolder, _progress._byteCount );
        else
            SW_LOG_WARNING( "Packaging failed at '%#': %#", _progress._stepName, _progress._failure );
    }

    void PackagingPanel::drawContent()
    {
        if ( _bGamesLoaded == SW_FALSE )
            refreshGames();
        collectResult();
        const bool bBusy = _pJob->isPending();

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
        Internal::drawOptionCombo( "Target", 120.0f * dpiScale, Internal::kArrTarget[_targetIndex], static_cast<uint32>( std::size( Internal::kArrTarget ) ),
                                   []( uint32 index )
        { return Internal::kArrTarget[index]; }, _targetIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Target" ) );
        ImGui::SameLine();
        const utf8* pGamePreview = _gameIndex < _listGame.size() ? _listGame[_gameIndex].c_str() : "(none)";
        Internal::drawOptionCombo( "Game", 160.0f * dpiScale, pGamePreview, static_cast<uint32>( _listGame.size() ),
                                   [this]( uint32 index )
        { return _listGame[index].c_str(); }, _gameIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Game" ) );
        ImGui::SameLine();
        Internal::drawOptionCombo( "RHI", 150.0f * dpiScale, Internal::kArrBackend[_rhiIndex], static_cast<uint32>( std::size( Internal::kArrBackend ) ),
                                   []( uint32 index )
        { return Internal::kArrBackend[index]; }, _rhiIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "RHIBackend" ) );

        EditorChrome::setNextItemWidthColumns( 43.0f );
        (void)EditorWidgets::drawTextField( "Output Folder", _outputFolder ); // 바뀐 값은 다음 Package 가 쓴다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "OutputFolder" ) );

        bool bSkipBuild = _bSkipBuild == SW_TRUE;
        if ( ImGui::Checkbox( "Skip build", &bSkipBuild ) )
            _bSkipBuild = bSkipBuild ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "packaging.skipBuild" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "SkipBuild" ) );
        bool bSkipCook = _bSkipCook == SW_TRUE;
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Skip cook" ) );
        if ( ImGui::Checkbox( "Skip cook", &bSkipCook ) )
            _bSkipCook = bSkipCook ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "packaging.skipCook" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "SkipCook" ) );
        ImGui::EndDisabled();
    }

    void PackagingPanel::drawActionRow( bool bBusy )
    {
        ImGui::BeginDisabled( bBusy );
        const bool bPackage = ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kBuildAll, "Package" ) );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "packaging.package" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "Package" ) );
        if ( bPackage )
            (void)startPackaging(); // 이미 돌면 단추가 꺼져 있다
        ImGui::SameLine();
        const bool bHasFolder = _progress._state == PackagingState::Succeeded && _progress._outputFolder.empty() == false;
        ImGui::BeginDisabled( bHasFolder == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kFolderOpen, "Open Folder" ) ) )
            (void)EditorAssetFileCommands::showInFileExplorer( _progress._outputFolder ); // 탐색기를 못 띄우면 경고가 남는다
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorPackaging", "OpenFolder" ) );
    }

    void PackagingPanel::drawProgressBar()
    {
        // 진행 — 단계 줄은 끝난 뒤 한꺼번에 오므로 도는 동안은 움직이는 막대다.
        fixed_string<constant::kMaxBuffer256> overlay;
        float32                               fraction = _progress.computeFraction();
        switch ( _progress._state )
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
                formatstring( overlay.data(), overlay.capacity(), "Done - %# MB", _progress._byteCount / constant::kBytesPerMegabyte );
                break;
            }
            case PackagingState::Failed:
            {
                formatstring( overlay.data(), overlay.capacity(), "Failed at %#", _progress._stepName.c_str() );
                break;
            }
        }
        ImGui::ProgressBar( fraction, ImVec2( -1.0f, 0.0f ), overlay.c_str() );
        if ( _progress._state == PackagingState::Failed && _progress._failure.empty() == false )
            EditorThemeUtil::textError( _progress._failure.c_str() );
        else if ( _progress._state == PackagingState::Succeeded )
            ImGui::TextDisabled( "%s", _progress._outputFolder.c_str() );
    }

    void PackagingPanel::drawLogList()
    {
        using Internal = PackagingPanelInternal;
        ImGui::Separator();
        if ( ImGui::BeginChild( "##packaging_log", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders ) )
        {
            const size_t firstLine = _listLine.size() > Internal::kVisibleLineCount ? _listLine.size() - Internal::kVisibleLineCount : 0;
            for ( size_t lineIndex = firstLine; lineIndex < _listLine.size(); ++lineIndex )
            {
                const string& line = _listLine[lineIndex];
                if ( Internal::isErrorLine( line ) )
                    EditorThemeUtil::textError( line.c_str() );
                else
                    ImGui::TextUnformatted( line.c_str() );
            }
        }
        ImGui::EndChild();
    }
} // namespace sw::editor
