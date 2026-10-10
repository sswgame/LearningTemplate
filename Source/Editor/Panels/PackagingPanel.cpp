#include "pch.h"

#include "Editor/Panels/PackagingPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorExternalToolJob.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Config/GameConfig.h"

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
            static constexpr const utf8* kScriptRelativePath = "Scripts/dev/MakePackage.py";
            /** @brief 진입점이 받는 타깃 이름입니다(`--target`). */
            static constexpr const utf8* kArrTarget[] = { "Client", "Server" };
            /** @brief 쿠킹 표의 백엔드 이름입니다(`--rhi` · `SW_SHIPPING_RHI_BACKEND`). 맨 앞은 "프리셋 기본값" 이다. */
#define SW_PACKAGING_BACKEND_NAME( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) #Backend,
            static constexpr const utf8* kArrBackend[] = { "(preset default)", SW_RHI_BACKEND_TABLE( SW_PACKAGING_BACKEND_NAME ) };
#undef SW_PACKAGING_BACKEND_NAME
            /** @brief 산출 폴더의 기본값입니다(저장소 루트 기준, git 무시). */
            static constexpr const utf8* kDefaultOutputFolder = "Saved/Packages";
            /** @brief 크기를 MB 로 보일 때의 나눗수입니다. */
            static constexpr uint64 kBytesPerMegabyte = 1024ull * 1024ull;
            /** @brief 로그가 보이는 최대 줄 수입니다. */
            static constexpr size_t kVisibleLineCount = 400;

            static string quote( string_view text ) { return "\"" + string{ text } + "\""; }

            /** @brief 오류처럼 보이는 줄입니다(빨강). */
            static bool isErrorLine( string_view line )
            {
                return StringUtil::contains( line, "FAILED", true ) || StringUtil::contains( line, "error", true );
            }

            static const PackagingPanel* findPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                return static_cast<const PackagingPanel*>( pContext->getPanelManager().findPanel( PackagingPanel::kPanelID ) );
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
    SW_AUTOMATION_PROBE( editorPackagingState, "Editor.PackagingState", "Packaging window: 0 idle, 1 running, 2 succeeded, 3 failed",
                         &PackagingPanelInternal::readPackagingState );

    PackagingPanel::PackagingPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        , _pJob{ make_unique<EditorExternalToolJob>() }
        , _listGame{}
        , _listLine{}
        , _outputFolder{ PackagingPanelInternal::kDefaultOutputFolder }
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
        (void)FileUtil::collectFiles( FileUtil::joinPath( EditorUtil::getProjectRootPath(), "Config/Game" ), "", listFile, false ); // 폴더가 없으면 게임이 없다
        const string& packRoot = GameConfig::getActive()._packRoot;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, ".json", true ) == false )
                continue;
            string text;
            if ( FileUtil::readTextFile( filePath, text ) && packRoot.empty() == false && StringUtil::contains( text, "\"" + packRoot + "\"", true ) )
                _gameIndex = static_cast<uint32>( _listGame.size() );
            _listGame.push_back( FileUtil::removeExtension( FileUtil::getFileNamePart( filePath ) ) );
        }
        _bGamesLoaded = SW_TRUE;
    }

    string PackagingPanel::makeCommand() const
    {
        using Internal           = PackagingPanelInternal;
        const string projectRoot = EditorUtil::getProjectRootPath();
        string       command     = string{ EditorUtil::kPythonCommand } + " " + Internal::quote( FileUtil::joinPath( projectRoot, Internal::kScriptRelativePath ) );
        command += string{ " --target " } + Internal::kArrTarget[_targetIndex];
        if ( _gameIndex < _listGame.size() )
            command += " --game " + _listGame[_gameIndex];
        if ( _rhiIndex > 0 )
            command += string{ " --rhi " } + Internal::kArrBackend[_rhiIndex];
        command += " --output " + Internal::quote( _outputFolder );
        if ( _bSkipBuild == SW_TRUE )
        {
            // 빌드를 건너뛰면 이 에디터의 Bin 을 스테이징한다(그 타깃의 Shipping 빌드가 없어도 흐름을 볼 수 있다).
            command += " --skip-build --bin-dir " + Internal::quote( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) );
        }
        if ( _bSkipCook == SW_TRUE )
            command += " --skip-cook";
        return command;
    }

    bool PackagingPanel::startPackaging()
    {
        if ( _pJob->request( makeCommand(), EditorUtil::getProjectRootPath() ) == false )
            return false;
        _listLine.clear();
        _progress        = PackagingProgress{};
        _progress._state = PackagingState::Running;
        SW_LOG_INFO( "Packaging started: %#", makeCommand() );
        return true;
    }

    void PackagingPanel::collectResult()
    {
        EditorExternalToolResult result{};
        if ( _pJob->take( result ) == false )
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
        using Internal = PackagingPanelInternal;
        if ( _bGamesLoaded == SW_FALSE )
            refreshGames();
        collectResult();
        const float32 dpiScale = EditorThemeUtil::getDpiScale();
        const bool    bBusy    = _pJob->isPending();

        ImGui::BeginDisabled( bBusy );
        ImGui::SetNextItemWidth( 120.0f * dpiScale );
        if ( ImGui::BeginCombo( "Target", Internal::kArrTarget[_targetIndex] ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( std::size( Internal::kArrTarget ) ); ++index )
            {
                if ( ImGui::Selectable( Internal::kArrTarget[index], index == _targetIndex ) )
                    _targetIndex = index;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 160.0f * dpiScale );
        const utf8* pGamePreview = _gameIndex < _listGame.size() ? _listGame[_gameIndex].c_str() : "(none)";
        if ( ImGui::BeginCombo( "Game", pGamePreview ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( _listGame.size() ); ++index )
            {
                if ( ImGui::Selectable( _listGame[index].c_str(), index == _gameIndex ) )
                    _gameIndex = index;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 150.0f * dpiScale );
        if ( ImGui::BeginCombo( "RHI", Internal::kArrBackend[_rhiIndex] ) )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( std::size( Internal::kArrBackend ) ); ++index )
            {
                if ( ImGui::Selectable( Internal::kArrBackend[index], index == _rhiIndex ) )
                    _rhiIndex = index;
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( "Shipping 이 정적으로 링크할 백엔드입니다(클라이언트만). 서버는 그리지 않는다" );

        ImGui::SetNextItemWidth( 320.0f * dpiScale );
        (void)EditorWidgets::drawTextField( "Output Folder", _outputFolder ); // 바뀐 값은 다음 Package 가 쓴다

        bool bSkipBuild = _bSkipBuild == SW_TRUE;
        if ( ImGui::Checkbox( "Skip build", &bSkipBuild ) )
            _bSkipBuild = bSkipBuild ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "packaging.skipBuild" );
        EditorWidgets::drawTooltip( "구성 · 빌드를 건너뛰고 이 에디터의 Bin 을 스테이징합니다" );
        ImGui::SameLine();
        bool bSkipCook = _bSkipCook == SW_TRUE;
        if ( ImGui::Checkbox( "Skip cook", &bSkipCook ) )
            _bSkipCook = bSkipCook ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "packaging.skipCook" );
        EditorWidgets::drawTooltip( "리소스 팩 쿠킹을 건너뜁니다(빌드를 하면 빌드가 쿠킹한다)" );
        ImGui::EndDisabled();

        ImGui::BeginDisabled( bBusy );
        const bool bPackage = ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kBuildAll, "Package" ) );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "packaging.package" );
        if ( bPackage )
            (void)startPackaging(); // 이미 돌면 단추가 꺼져 있다
        ImGui::SameLine();
        const bool bHasFolder = _progress._state == PackagingState::Succeeded && _progress._outputFolder.empty() == false;
        ImGui::BeginDisabled( bHasFolder == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kFolderOpen, "Open Folder" ) ) )
            (void)EditorAssetCommands::showInFileExplorer( _progress._outputFolder ); // 탐색기를 못 띄우면 경고가 남는다
        ImGui::EndDisabled();

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
                formatstring( overlay.data(), overlay.capacity(), "Done - %# MB", _progress._byteCount / Internal::kBytesPerMegabyte );
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
