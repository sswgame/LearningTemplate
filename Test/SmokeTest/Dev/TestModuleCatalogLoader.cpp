/**
 * @file TestModuleCatalogLoader.cpp
 * @brief ModuleCatalogLoader — 이 빌드의 `Bin/Modules` · `Bin/Games` 매니페스트로 호스트가 올릴 모듈을 정하는지.
 * @details 빌드(CMake)가 정한 로드 순서에서 "고른 게임이 쓰지 않는" 키트만 빠져야 하고, 함께 지은 게임 팩을 고르면 그 게임의 키트는 모두 켜지고
 *          다른 게임 팩만 쓰는 키트는 꺼져야 한다. Shipping(매니페스트 없음)과 게임 팩을 짓지 않은 빌드는 건너뛴다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"

#include "ModuleHost/ModuleCatalogLoader.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"

namespace
{
    /** @brief 로더가 빼고 "not used by" 로 적은 모듈인지입니다. */
    bool isRemovedAsUnused( const sw::ModuleResolution& resolution, const sw::string& name )
    {
        for ( const sw::ModuleInactiveEntry& inactive : resolution._listInactive )
        {
            if ( inactive._name == name && sw::StringUtil::startsWith( inactive._reason, "not used by" ) )
                return true;
        }
        return false;
    }

    /** @brief 고른 게임 팩(`Games/<게임>`)을 되돌리는 범위입니다. */
    struct ScopedGamePack
    {
        explicit ScopedGamePack( const sw::string& packDirectory )
            : _bHadMovedModule{ sw::ModuleImageUtil::getModuleSubdirectory( _previousModule, _previousDirectory ) }
        {
            sw::ModuleImageUtil::setModuleSubdirectory( sw::config::kTargetGameModule, packDirectory );
        }

        ~ScopedGamePack()
        {
            if ( _bHadMovedModule )
                sw::ModuleImageUtil::setModuleSubdirectory( _previousModule, _previousDirectory );
            else
                sw::ModuleImageUtil::setModuleSubdirectory( sw::config::kTargetGameModule, {} );
        }

        sw::string _previousModule;
        sw::string _previousDirectory;
        bool       _bHadMovedModule;
    };
} // namespace

/**
 * @brief [ModuleCatalogLoaderTest] 로더의 로드 순서는 빌드(CMake `ResolvedModules.txt`)의 순서에서 고른 게임이 쓰지 않는 키트만 뺀 것이다
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, LoadOrderIsTheBuildOrderWithoutUnusedKits )
{
    const sw::string resolvedPath = sw::FileUtil::joinPath( sw::ModuleImageUtil::getModuleDirectory(), "ResolvedModules.txt" );
    if ( sw::FileUtil::exists( resolvedPath ) == false )
        SW_TEST_SKIP( "no module catalog in Bin/Modules (Shipping does not copy manifests)" );

    sw::ModuleCatalog    catalog;
    sw::ModuleResolution resolution;
    SW_ASSERT_TRUE( sw::ModuleCatalogLoader::loadAndResolve( sw::ModuleCatalog::getBuildTargetMask(), catalog, resolution ) );

    sw::string buildText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( resolvedPath, buildText ) );
    const sw::string buildOrder = sw::StringUtil::replace( buildText, "\r\n", "\n" );
    sw::string       expectedOrder;
    size_t           lineStart = 0;
    while ( lineStart < buildOrder.size() )
    {
        size_t lineEnd = buildOrder.find( '\n', lineStart );
        if ( lineEnd == sw::string::npos )
            lineEnd = buildOrder.size();
        const sw::string name = buildOrder.substr( lineStart, lineEnd - lineStart );
        if ( name.empty() == false && isRemovedAsUnused( resolution, name ) == false )
            expectedOrder += name + "\n";
        lineStart = lineEnd + 1;
    }
    sw::string runtimeOrder;
    for ( const sw::string& name : resolution._listLoadOrder )
    {
        runtimeOrder += name + "\n";
    }
    SW_EXPECT_STREQ( expectedOrder.c_str(), runtimeOrder.c_str() );
}

/**
 * @brief [ModuleCatalogLoaderTest] 게임 팩을 고르면 그 게임의 의존은 모두 켜지고, 뺀 키트는 그 게임이 쓰지 않는 것이다
 * @details 게임 팩이 둘 이상이고 쓰는 키트가 다르면 어느 게임을 고르든 다른 게임의 키트 하나는 빠진다 — 빠진 것이 하나도 없으면 로더가 거르지 않는 것이다.
 */
SW_TEST_CASE( ModuleCatalogLoaderTest, SelectedGamePackLoadsOnlyItsOwnGameKits )
{
    const sw::string gamesDirectory = sw::FileUtil::joinPath( sw::FileUtil::getBinaryDirectory(), sw::config::kDirBinGames );
    if ( sw::FileUtil::isDirectory( gamesDirectory ) == false || sw::FileUtil::isDirectory( sw::ModuleImageUtil::getModuleDirectory() ) == false )
        SW_TEST_SKIP( "no game packs in Bin/Games (SW_GAMES is empty or Shipping)" );

    sw::vector<sw::string> listFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( gamesDirectory, ".json", listFile, true ) );
    uint32 removedCount = 0;
    for ( const sw::string& filePath : listFile )
    {
        if ( sw::StringUtil::endsWith( filePath, sw::ModuleCatalog::kManifestExtension, true ) == false )
            continue;
        sw::string         text;
        sw::string         error;
        sw::ModuleManifest pack;
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( filePath, text ) );
        SW_ASSERT_TRUE_MSG( sw::ModuleCatalog::parseManifest( text, filePath, pack, error ), error.c_str() );
        const sw::string     gameName = sw::FileUtil::getFileNamePart( sw::FileUtil::getDirectoryPart( filePath ) );
        const ScopedGamePack scope{ sw::FileUtil::joinPath( sw::config::kDirBinGames, gameName ) };

        sw::ModuleCatalog    catalog;
        sw::ModuleResolution resolution;
        SW_ASSERT_TRUE_MSG( sw::ModuleCatalogLoader::loadAndResolve( sw::ModuleCatalog::getBuildTargetMask(), catalog, resolution ), gameName.c_str() );
        for ( const sw::ModuleDependency& dependency : pack._listDependency )
        {
            SW_EXPECT_TRUE_MSG( resolution.isActive( dependency._name ), ( gameName + " needs " + dependency._name ).c_str() );
        }
        for ( const sw::ModuleInactiveEntry& inactive : resolution._listInactive )
        {
            if ( isRemovedAsUnused( resolution, inactive._name ) )
                ++removedCount;
        }
    }
    SW_EXPECT_TRUE( removedCount > 0 );
}
