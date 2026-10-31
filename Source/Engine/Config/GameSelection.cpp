#include "pch.h"

#include "Engine/Config/GameSelection.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SW_LOG_CALLER( "GameSelection" );

    namespace
    {
        struct GameSelectionInternal
        {
            /**
             * @brief 고른 게임 이름 — 고정 배열이라 상수 초기화되고 정적 소멸 · 할당자 순서와 엮이지 않는다(`ModuleLibraryUtil` 의 옮긴 모듈과 같다).
             * @details 코어 범위의 `CrashHandlerSubsystem` 이 기동 스레드에서 한 번 적고 그 뒤에 읽는다. 읽는 쪽(모듈 · 설정 경로를 찾는 정적 함수)이 서비스 범위가 서기 전에도 묻고
             *          테스트도 범위 없이 묻는다(고르기 전이면 기본 게임) — 그래서 범위에 맡기지 않고 이 변수 하나가 소유한다. 지연 생성하는 접근자를 두지 않는다.
             */
            static inline utf8 s_arrName[GameSelection::kNameCapacity]{};
        };
    } // namespace

    string GameSelection::readDefaultName()
    {
        if constexpr ( build::kShipping )
            return string{ config::kShippingGameName };

        string       text;
        const string path = FileUtil::joinPath( ModuleLibraryUtil::getModuleDirectory(), config::kFileActiveGame );
        if ( FileUtil::readTextFile( path, text ) == false )
            return {};
        return string{ StringUtil::trim( text ) };
    }

    bool GameSelection::select( const CommandLineManager& commandLine )
    {
        // 배포는 베이크한 게임 하나이고 `-game=` 을 보지 않는다(모듈은 정적 링크).
        if constexpr ( build::kShipping )
        {
            storeName( config::kShippingGameName );
            return true;
        }

        const string activeGame = readDefaultName();
        string       name;
        if ( commandLine.getArgument( CommandLineArgument::GAME, name ) == false || name.empty() )
            name = activeGame;
        if ( name.empty() )
        {
            SW_LOG_ERROR( "No game selected - %# is missing next to the modules; pass -game=<name>", config::kFileActiveGame );
            return false;
        }
        if ( name.size() >= kNameCapacity )
        {
            SW_LOG_ERROR( "Game name '%#' is too long", name.c_str() );
            return false;
        }

        // 활성 게임은 `Bin/Modules/SWGame` 이다. 다른 게임은 같은 빌드가 함께 지은 게임 팩(`Bin/Games/<이름>/SWGame`, CMake `SW_GAMES`)이어야 한다.
        if ( name == activeGame )
        {
            ModuleLibraryUtil::setModuleSubdirectory( config::kTargetGameModule, {} );
        }
        else
        {
            const string packDirectory = FileUtil::joinPath( config::kDirBinGames, name );
            const string libraryPath   = FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._binary.str(), packDirectory ),
                                                             ModuleLibraryUtil::formatSharedLibraryName( config::kTargetGameModule ) );
            if ( FileUtil::exists( libraryPath ) == false )
            {
                SW_LOG_ERROR( "Game '%#' is not built in this build (%# is missing) - add it to SW_GAMES or use its preset", name.c_str(), libraryPath.c_str() );
                return false;
            }
            ModuleLibraryUtil::setModuleSubdirectory( config::kTargetGameModule, packDirectory );
        }
        storeName( name );
        SW_LOG_INFO( "Game %# (build active game %#)", name.c_str(), activeGame.c_str() );
        return true;
    }

    void GameSelection::storeName( string_view name )
    {
        StringUtil::strncpy( GameSelectionInternal::s_arrName, string{ name }.c_str(), kNameCapacity );
    }

    string GameSelection::getName()
    {
        const utf8* pName = GameSelectionInternal::s_arrName;
        if ( pName[0] != '\0' )
            return string{ pName };
        return readDefaultName();
    }

    string GameSelection::getConfigPath()
    {
        return FileUtil::joinPath( config::kDirRuntimeGamePreset, getName() + ".json" );
    }

    string GameSelection::getServerConfigPath()
    {
        return FileUtil::joinPath( config::kDirRuntimeServerPreset, getName() + ".json" );
    }
} // namespace sw
