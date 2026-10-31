/**
 * @file GameSelectionBuild.cpp
 * @brief 개발 구성의 게임 고르기 — `-game=<이름>` 또는 빌드의 활성 게임(`Bin/Modules/ActiveGame.txt`), 활성 게임이 아니면 게임 팩(`Bin/Games/<이름>/`)에서 모듈을 올린다.
 * @details 배포는 `GameSelectionBuildShipping.cpp`(베이크한 게임 하나)를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`).
 */
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SW_LOG_CALLER( "GameSelection" );

    string GameSelection::readDefaultName()
    {
        string       text;
        const string path = FileUtil::joinPath( ModuleLibraryUtil::getModuleDirectory(), config::kFileActiveGame );
        if ( FileUtil::readTextFile( path, text ) == false )
            return {};
        return string{ StringUtil::trim( text ) };
    }

    bool GameSelection::select( const CommandLineManager& commandLine )
    {
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
} // namespace sw
