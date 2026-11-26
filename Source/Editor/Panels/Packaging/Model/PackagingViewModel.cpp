#include "pch.h"

#include "Editor/Panels/Packaging/Model/PackagingViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"

#include "sw/config/CookContract.gen.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 진입점(저장소 루트 기준)입니다. */
        constexpr const utf8* kScriptRelativePath = "Scripts/package/pack.py";
        /** @brief 진입점이 받는 플랫폼 이름입니다(`--platform`, `pack.py` 의 `MAP_PLATFORM_PRESET`). 맨 앞이 이 호스트다. */
        constexpr const utf8* kArrPlatform[] = { "Desktop", "Web", "Android" };
        /** @brief 진입점이 받는 타깃 이름입니다(`--target`). */
        constexpr const utf8* kArrTarget[] = { "Client", "Server" };
        /** @brief 쿠킹 테이블의 백엔드 이름입니다(`--rhi` · `SW_SHIPPING_RHI_BACKEND`). 맨 앞은 "프리셋 기본값" 이다. */
#define SW_PACKAGING_BACKEND_NAME( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) #Backend,
        constexpr const utf8* kArrBackend[] = { "(preset default)", SW_RHI_BACKEND_TABLE( SW_PACKAGING_BACKEND_NAME ) };
#undef SW_PACKAGING_BACKEND_NAME
    } // namespace

    PackagingViewModel::PackagingViewModel()
        : _pJob{ make_unique<EditorExternalToolJob>() }
        , _listGame{}
        , _listLine{}
        , _outputFolder{} // 비면 진입점의 기본 산출 폴더(`pack.py --output` 의 기본값이 원본이다)
        , _progress{}
        , _gameIndex{ 0 }
        , _platformIndex{ 0 }
        , _targetIndex{ 0 }
        , _rhiIndex{ 0 }
        , _bSkipBuild{ false }
        , _bSkipCook{ false }
        , _bGamesLoaded{ SW_FALSE }
    {
    }

    PackagingViewModel::~PackagingViewModel()
    {
        if ( _pJob != nullptr )
            _pJob->cancelAndWait();
    }

    void PackagingViewModel::cancelAndWait()
    {
        _pJob->cancelAndWait();
    }

    void PackagingViewModel::update()
    {
        if ( _bGamesLoaded == false )
            refreshGames();
        collectResult();
    }

    bool PackagingViewModel::isBusy() const
    {
        return _pJob->isPending();
    }

    uint32 PackagingViewModel::getPlatformCount()
    {
        return static_cast<uint32>( SW_COUNT_OF( kArrPlatform ) );
    }

    const utf8* PackagingViewModel::getPlatformName( uint32 index )
    {
        return kArrPlatform[index];
    }

    uint32 PackagingViewModel::getTargetCount()
    {
        return static_cast<uint32>( SW_COUNT_OF( kArrTarget ) );
    }

    const utf8* PackagingViewModel::getTargetName( uint32 index )
    {
        return kArrTarget[index];
    }

    uint32 PackagingViewModel::getBackendCount()
    {
        return static_cast<uint32>( SW_COUNT_OF( kArrBackend ) );
    }

    const utf8* PackagingViewModel::getBackendName( uint32 index )
    {
        return kArrBackend[index];
    }

    bool PackagingViewModel::isErrorLine( string_view line )
    {
        // 낱말 "error" 가 든 줄이 아니라 도구가 오류로 찍은 줄만이다(감사 E-15) — "0 errors" · "error_log.cpp" 를 빨갛게 칠하지 않는다.
        // 진입점의 실패 줄(`[package] failed`), ninja 의 `FAILED:`, 컴파일러 · 링커 · 파이썬의 `error:` · `: error ` · `error C1234` · `error LNK1234` · `Error:` 꼴이다.
        PackagingProgress progress{};
        if ( PackagingProgress::parseLine( line, progress ) && progress._state == PackagingState::Failed )
            return true;
        const string_view trimmed = StringUtil::trim( line );
        if ( StringUtil::startsWith( trimmed, "FAILED:" ) || StringUtil::startsWith( trimmed, "error:" ) || StringUtil::startsWith( trimmed, "Error:" ) )
            return true;
        return StringUtil::contains( line, ": error ", false ) || StringUtil::contains( line, ": error:", false ) || StringUtil::contains( line, ": fatal error", false );
    }

    void PackagingViewModel::refreshGames()
    {
        _listGame.clear();
        _gameIndex = 0;
        vector<AbsolutePath> listFile;
        (void)FileUtil::collectFiles( ProcessRoots::get()._project.join( "Config/Game" ), "", listFile, false ); // 폴더가 없으면 게임이 없다
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        for ( const AbsolutePath& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath.str(), ".json", true ) == false )
                continue;
            string                                  text;
            fixed_string<constant::buffer::kMax256> quotedRoot;
            formatstring( quotedRoot.data(), quotedRoot.capacity(), "\"%#\"", packRoot );
            const bool bGameUsesPackRoot = FileUtil::readTextFile( filePath, text ) && packRoot.empty() == false && StringUtil::contains( text, quotedRoot, true );
            if ( bGameUsesPackRoot )
                _gameIndex = static_cast<uint32>( _listGame.size() );
            _listGame.push_back( FileUtil::removeExtension( FileUtil::getFileNamePart( filePath.str() ) ) );
        }
        _bGamesLoaded = SW_TRUE;
    }

    ProcessCommand PackagingViewModel::makeCommand() const
    {
        const AbsolutePath projectRoot  = ProcessRoots::get()._project;
        ProcessCommand     command      = EditorExternalToolJob::makePythonScriptCommand( projectRoot.join( kScriptRelativePath ).str() );
        vector<string>&    listArgument = command._listArgument;
        listArgument.insert( listArgument.end(), { "--platform", kArrPlatform[_platformIndex] } );
        if ( isDesktopPlatform() )
            listArgument.insert( listArgument.end(), { "--target", kArrTarget[_targetIndex] } );
        if ( _gameIndex < _listGame.size() )
            listArgument.insert( listArgument.end(), { "--game", _listGame[_gameIndex] } );
        if ( isDesktopPlatform() && _rhiIndex > 0 )
            listArgument.insert( listArgument.end(), { "--rhi", kArrBackend[_rhiIndex] } );
        if ( _outputFolder.empty() == false )
            listArgument.insert( listArgument.end(), { "--output", _outputFolder } );
        if ( _bSkipBuild )
        {
            listArgument.push_back( "--skip-build" );
            // 빌드를 건너뛰면 이 에디터의 Bin 을 스테이징한다(그 타깃의 Shipping 빌드가 없어도 흐름을 볼 수 있다). 다른 플랫폼은 그 프리셋의 Bin 이다 — 이 에디터의 산출물이 아니다.
            if ( isDesktopPlatform() )
                listArgument.insert( listArgument.end(), { "--bin-dir", FileUtil::getDirectoryPart( FileUtil::getExecutablePath().str() ) } );
        }
        if ( _bSkipCook )
            listArgument.push_back( "--skip-cook" );
        return command;
    }

    bool PackagingViewModel::startPackaging()
    {
        const ProcessCommand command = makeCommand();
        if ( _pJob->request( command, ProcessRoots::get()._project.str() ) == false )
            return false;
        _listLine.clear();
        _liveLineCount   = 0;
        _progress        = PackagingProgress{};
        _progress._state = PackagingState::Running;
        SW_LOG_INFO_ID( "PackagingPanel.PackagingStarted", "Packaging started: %#", command.toCommandLine() );
        return true;
    }

    void PackagingViewModel::collectResult()
    {
        EditorExternalToolResult result{};
        if ( _pJob->take( result ) != EditorTaskPoll::Ready )
        {
            // 도는 동안의 줄을 바로 보인다(감사 E-15) — 단계 줄(`[package] step k/n`)이 오면 진행도도 따라간다. 끝난 결과가 모든 줄과 판정으로 바꾼다.
            if ( _progress._state != PackagingState::Running )
                return;
            const size_t lineCountBefore = _listLine.size();
            _pJob->takeLines( _listLine );
            _liveLineCount += static_cast<uint32>( _listLine.size() - lineCountBefore );
            for ( size_t lineIndex = lineCountBefore; lineIndex < _listLine.size(); ++lineIndex )
                (void)PackagingProgress::parseLine( _listLine[lineIndex], _progress ); // 진입점이 찍지 않은 줄(빌드 · 쿠커 출력)은 지나친다
            if ( _progress._state != PackagingState::Running )
                _progress._state = PackagingState::Running; // 판정(성공 · 실패)은 종료 코드와 함께 끝난 결과가 한다
            return;
        }
        _listLine             = std::move( result._listLine );
        _progress             = PackagingProgress::parseOutput( _listLine, result._exitCode );
        const bool bSucceeded = _progress._state == PackagingState::Succeeded;
        if ( bSucceeded )
            SW_LOG_INFO_ID( "Packaging.Done", "Packaging done: %# (%# bytes)", _progress._outputFolder, _progress._byteCount );
        else
            SW_LOG_WARNING_ID( "PackagingPanel.PackagingFailed", "Packaging failed at '%#': %#", _progress._stepName, _progress._failure );
    }

    bool PackagingViewModel::openOutputFolder() const
    {
        return EditorAssetFileCommands::showInFileExplorer( _progress._outputFolder );
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct PackagingProgressParserInternal
        {
            static constexpr string_view kPrefix     = "[package] ";
            static constexpr string_view kStepWord   = "step ";
            static constexpr string_view kDoneWord   = "done ";
            static constexpr string_view kFailedWord = "FAILED ";

            /** @brief 앞의 10 진수를 읽고 그 뒤를 @p outRest 에 둡니다. 숫자가 없으면 false 입니다. */
            [[nodiscard]] static bool readUnsigned( string_view text, uint64& outValue, string_view& outRest )
            {
                size_t digitCount = 0;
                outValue          = 0;
                while ( digitCount < text.size() && StringUtil::isDigitChar( text[digitCount] ) )
                {
                    outValue = outValue * 10 + static_cast<uint64>( text[digitCount] - '0' );
                    ++digitCount;
                }
                outRest = text.substr( digitCount );
                return digitCount > 0;
            }

            /** @brief `k/n <단계>` 를 읽습니다. */
            [[nodiscard]] static bool parseStep( string_view body, PackagingProgress& inoutProgress )
            {
                uint64      stepIndex = 0;
                uint64      stepCount = 0;
                string_view rest;
                const bool  bMalformedStepIndex = readUnsigned( body, stepIndex, rest ) == false || rest.empty() || rest.front() != '/';
                if ( bMalformedStepIndex )
                    return false;
                const bool bMalformedStepCount = readUnsigned( rest.substr( 1 ), stepCount, rest ) == false || stepIndex == 0 || stepCount < stepIndex;
                if ( bMalformedStepCount )
                    return false;
                inoutProgress._stepIndex = static_cast<uint32>( stepIndex );
                inoutProgress._stepCount = static_cast<uint32>( stepCount );
                inoutProgress._stepName  = string{ StringUtil::trim( rest ) };
                inoutProgress._state     = PackagingState::Running;
                return true;
            }

            /** @brief `<폴더> <바이트>` 를 읽습니다. 폴더에 공백이 있을 수 있어 바이트는 마지막 낱말이다. */
            [[nodiscard]] static bool parseDone( string_view body, PackagingProgress& inoutProgress )
            {
                const size_t lastSpace = body.find_last_of( ' ' );
                if ( lastSpace == string_view::npos )
                    return false;
                uint64      byteCount = 0;
                string_view rest;
                if ( readUnsigned( body.substr( lastSpace + 1 ), byteCount, rest ) == false || rest.empty() == false )
                    return false;
                inoutProgress._outputFolder = string{ body.substr( 0, lastSpace ) };
                inoutProgress._byteCount    = byteCount;
                inoutProgress._stepIndex    = inoutProgress._stepCount;
                inoutProgress._state        = PackagingState::Succeeded;
                return true;
            }

            /** @brief `<단계> <이유>` 를 읽습니다. */
            [[nodiscard]] static bool parseFailed( string_view body, PackagingProgress& inoutProgress )
            {
                const size_t space      = body.find( ' ' );
                inoutProgress._stepName = string{ body.substr( 0, space ) };
                inoutProgress._failure  = space == string_view::npos ? string{} : string{ body.substr( space + 1 ) };
                inoutProgress._state    = PackagingState::Failed;
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 PackagingProgress::computeFraction() const
    {
        if ( _state == PackagingState::Succeeded || _state == PackagingState::Failed )
            return 1.0f;
        if ( _stepCount == 0 )
            return 0.0f;
        // 시작한 단계는 반쯤 온 것으로 본다 — 단계 안의 진행은 줄로 오지 않는다.
        return ( static_cast<float32>( _stepIndex ) - 0.5f ) / static_cast<float32>( _stepCount );
    }

    bool PackagingProgress::parseLine( string_view line, PackagingProgress& inoutProgress )
    {
        using Internal            = PackagingProgressParserInternal;
        const string_view trimmed = StringUtil::trim( line );
        if ( StringUtil::startsWith( trimmed, Internal::kPrefix ) == false )
            return false;
        const string_view body = trimmed.substr( Internal::kPrefix.size() );
        if ( StringUtil::startsWith( body, Internal::kStepWord ) )
            return Internal::parseStep( body.substr( Internal::kStepWord.size() ), inoutProgress );
        if ( StringUtil::startsWith( body, Internal::kDoneWord ) )
            return Internal::parseDone( body.substr( Internal::kDoneWord.size() ), inoutProgress );
        if ( StringUtil::startsWith( body, Internal::kFailedWord ) )
            return Internal::parseFailed( body.substr( Internal::kFailedWord.size() ), inoutProgress );
        return false;
    }

    PackagingProgress PackagingProgress::parseOutput( const vector<string>& listLine, const optional<int32>& exitCode )
    {
        PackagingProgress progress{};
        for ( const string& line : listLine )
        {
            (void)parseLine( line, progress ); // 진입점이 찍지 않은 줄(빌드 · 쿠커 출력)은 지나친다
        }
        const bool bFinishedWithoutResult = progress._state == PackagingState::Idle || progress._state == PackagingState::Running;
        if ( bFinishedWithoutResult || ( progress._state == PackagingState::Succeeded && exitCode != 0 ) )
        {
            progress._state   = PackagingState::Failed;
            progress._failure = listLine.empty() ? string{ "the packaging script printed nothing (is Python on PATH?)" } : listLine.back();
        }
        return progress;
    }
} // namespace sw::editor
