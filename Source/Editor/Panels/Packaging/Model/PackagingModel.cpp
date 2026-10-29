#include "pch.h"

#include "Editor/Panels/Packaging/Model/PackagingModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Task/EditorExternalToolJob.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"

#include "sw/config/CookContract.gen.h"

namespace sw::editor
{
    SW_LOG_CALLER( "PackagingPanel" );

    namespace
    {
        /** @brief 진입점(저장소 루트 기준)입니다. */
        constexpr const utf8* kScriptRelativePath = "Scripts/package/Pack.py";
        /** @brief 진입점이 받는 타깃 이름입니다(`--target`). */
        constexpr const utf8* kArrTarget[] = { "Client", "Server" };
        /** @brief 쿠킹 테이블의 백엔드 이름입니다(`--rhi` · `SW_SHIPPING_RHI_BACKEND`). 맨 앞은 "프리셋 기본값" 이다. */
#define SW_PACKAGING_BACKEND_NAME( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) #Backend,
        constexpr const utf8* kArrBackend[] = { "(preset default)", SW_RHI_BACKEND_TABLE( SW_PACKAGING_BACKEND_NAME ) };
#undef SW_PACKAGING_BACKEND_NAME
    } // namespace

    PackagingModel::PackagingModel()
        : _pJob{ make_unique<EditorExternalToolJob>() }
        , _listGame{}
        , _listLine{}
        , _outputFolder{} // 비면 진입점의 기본 산출 폴더(`Pack.py --output` 의 기본값이 원본이다)
        , _progress{}
        , _gameIndex{ 0 }
        , _targetIndex{ 0 }
        , _rhiIndex{ 0 }
        , _bSkipBuild{ false }
        , _bSkipCook{ false }
        , _bGamesLoaded{ false }
    {
    }

    PackagingModel::~PackagingModel()
    {
        if ( _pJob != nullptr )
            _pJob->cancelAndWait();
    }

    void PackagingModel::cancelAndWait()
    {
        _pJob->cancelAndWait();
    }

    void PackagingModel::update()
    {
        if ( _bGamesLoaded == false )
            refreshGames();
        collectResult();
    }

    bool PackagingModel::isBusy() const
    {
        return _pJob->isPending();
    }

    uint32 PackagingModel::getTargetCount()
    {
        return static_cast<uint32>( std::size( kArrTarget ) );
    }

    const utf8* PackagingModel::getTargetName( uint32 index )
    {
        return kArrTarget[index];
    }

    uint32 PackagingModel::getBackendCount()
    {
        return static_cast<uint32>( std::size( kArrBackend ) );
    }

    const utf8* PackagingModel::getBackendName( uint32 index )
    {
        return kArrBackend[index];
    }

    bool PackagingModel::isErrorLine( string_view line )
    {
        return StringUtil::contains( line, "FAILED", true ) || StringUtil::contains( line, "error", true );
    }

    void PackagingModel::refreshGames()
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
        _bGamesLoaded = true;
    }

    ProcessCommand PackagingModel::makeCommand() const
    {
        const string    projectRoot  = EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() );
        ProcessCommand  command      = EditorUtil::makePythonScriptCommand( FileUtil::joinPath( projectRoot, kScriptRelativePath ) );
        vector<string>& listArgument = command._listArgument;
        listArgument.insert( listArgument.end(), { "--target", kArrTarget[_targetIndex] } );
        if ( _gameIndex < _listGame.size() )
            listArgument.insert( listArgument.end(), { "--game", _listGame[_gameIndex] } );
        if ( _rhiIndex > 0 )
            listArgument.insert( listArgument.end(), { "--rhi", kArrBackend[_rhiIndex] } );
        if ( _outputFolder.empty() == false )
            listArgument.insert( listArgument.end(), { "--output", _outputFolder } );
        if ( _bSkipBuild )
        {
            // 빌드를 건너뛰면 이 에디터의 Bin 을 스테이징한다(그 타깃의 Shipping 빌드가 없어도 흐름을 볼 수 있다).
            listArgument.insert( listArgument.end(), { "--skip-build", "--bin-dir", FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) } );
        }
        if ( _bSkipCook )
            listArgument.push_back( "--skip-cook" );
        return command;
    }

    bool PackagingModel::startPackaging()
    {
        const ProcessCommand command = makeCommand();
        if ( _pJob->request( command, EditorPaths::getProjectRootPath( editor::getSubsystem<AssetPaths>() ) ) == false )
            return false;
        _listLine.clear();
        _progress        = PackagingProgress{};
        _progress._state = PackagingState::Running;
        SW_LOG_INFO( "Packaging started: %#", command.toCommandLine() );
        return true;
    }

    void PackagingModel::collectResult()
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
} // namespace sw::editor
