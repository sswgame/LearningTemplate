#include "pch.h"

#include "ModuleManager/ModuleCatalogLoader.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleImageUtil.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleCatalog" );

    namespace
    {
        struct ModuleCatalogLoaderInternal
        {
            static constexpr size_t kNotFound = invalid_index::kUint64;

            /** @brief 매니페스트 파일 하나를 읽습니다. 못 읽거나 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readManifest( const string& manifestPath, ModuleManifest& outManifest )
            {
                string text;
                string error;
                if ( FileUtil::readTextFile( manifestPath, text ) == false )
                {
                    SW_LOG_ERROR( "Module catalog: cannot read %#", manifestPath.c_str() );
                    return false;
                }
                if ( ModuleCatalog::parseManifest( text, manifestPath, outManifest, error ) == false )
                {
                    SW_LOG_ERROR( "Module catalog: %#", error.c_str() );
                    return false;
                }
                return true;
            }

            /** @brief 폴더의 `*.module.json` 을 읽습니다(@p bRecursive 면 하위 폴더까지, 경로 순). 폴더가 없으면 빈 목록으로 true 입니다. */
            [[nodiscard]] static bool readManifestFolder( const string& directoryPath, bool bRecursive, vector<ModuleManifest>& outListManifest )
            {
                vector<string> listFile;
                if ( FileUtil::isDirectory( directoryPath ) == false )
                    return true;
                if ( FileUtil::collectFiles( directoryPath, ".json", listFile, bRecursive ) == false )
                {
                    SW_LOG_ERROR( "Module catalog: cannot list %#", directoryPath.c_str() );
                    return false;
                }
                std::sort( listFile.begin(), listFile.end() );
                for ( const string& filePath : listFile )
                {
                    if ( StringUtil::endsWith( filePath, ModuleCatalog::kManifestExtension, true ) == false )
                        continue;
                    ModuleManifest manifest;
                    if ( readManifest( filePath, manifest ) == false )
                        return false;
                    outListManifest.push_back( std::move( manifest ) );
                }
                return true;
            }

            /** @brief @p listManifest 에서 이름으로 찾은 칸 번호입니다. 없으면 `kNotFound` 입니다. */
            static size_t findIndex( const vector<ModuleManifest>& listManifest, string_view name )
            {
                for ( size_t index = 0; index < listManifest.size(); ++index )
                {
                    if ( listManifest[index]._name == name )
                        return index;
                }
                return kNotFound;
            }

            /** @brief @p listSeed 에서 `_listDependency` 를 따라 닿는 모듈을 @p inoutListReached(@p listManifest 칸 순서)에 표시합니다. */
            static void markReached( const vector<ModuleManifest>& listManifest, vector<string_view> listSeed, vector<uint8>& inoutListReached )
            {
                while ( listSeed.empty() == false )
                {
                    const size_t index = findIndex( listManifest, listSeed.back() );
                    listSeed.pop_back();
                    if ( index == kNotFound || inoutListReached[index] == SW_TRUE )
                        continue; // 없는 의존은 resolve 가 오류로 알린다
                    inoutListReached[index] = SW_TRUE;
                    for ( const ModuleDependency& dependency : listManifest[index]._listDependency )
                    {
                        listSeed.push_back( dependency._name );
                    }
                }
            }

            /** @brief 게임 매니페스트가 쓰는 모듈(의존과 표가 켠 것)을 @p inoutListSeed 에 더합니다. */
            static void appendGameSeeds( const ModuleManifest& game, vector<string_view>& inoutListSeed )
            {
                for ( const ModuleDependency& dependency : game._listDependency )
                {
                    inoutListSeed.push_back( dependency._name );
                }
                for ( const ModuleOverride& entry : game._listModuleOverride )
                {
                    if ( entry._bEnabled )
                        inoutListSeed.push_back( entry._name );
                }
            }

            /**
             * @brief 고른 게임이 닿지 않고 함께 지은 게임 팩만 닿는 키트, 그리고 그런 키트에 기대는 키트 · 에디터 확장을 @p inoutListManifest 에서 빼고 @p outListRemoved 에 적습니다.
             * @details 쓰는 쪽 = 키트가 아닌 모듈(프로젝트 · 에디터 · RHI · GameFramework)과 고른 게임의 표가 켠 키트다(CMake `sw_collectUnusedKits` 와 같다).
             *          아무 게임도 닿지 않는 키트(`SW_BUILD_UNUSED_KITS=ON` 으로 지은 것)는 남긴다.
             */
            static void removeUnreachedGamePackKits( vector<ModuleManifest>& inoutListManifest, const vector<ModuleManifest>& listGamePackProject, const string& gameName,
                                                     vector<ModuleInactiveEntry>& outListRemoved )
            {
                if ( listGamePackProject.empty() )
                    return;
                const size_t        manifestCount = inoutListManifest.size();
                vector<string_view> listUsedSeed;
                for ( const ModuleManifest& manifest : inoutListManifest )
                {
                    if ( manifest._kind == ModuleKind::Game )
                        appendGameSeeds( manifest, listUsedSeed );
                    if ( manifest._kind != ModuleKind::Kit && manifest._kind != ModuleKind::EditorExtension )
                        listUsedSeed.push_back( manifest._name );
                }
                vector<string_view> listPackSeed;
                for ( const ModuleManifest& project : listGamePackProject )
                {
                    appendGameSeeds( project, listPackSeed );
                }
                vector<uint8> listUsed( manifestCount, SW_FALSE );
                vector<uint8> listRemoved( manifestCount, SW_FALSE );
                markReached( inoutListManifest, std::move( listUsedSeed ), listUsed );
                markReached( inoutListManifest, std::move( listPackSeed ), listRemoved );
                for ( size_t index = 0; index < manifestCount; ++index )
                {
                    const bool bKit    = inoutListManifest[index]._kind == ModuleKind::Kit;
                    listRemoved[index] = ( bKit && listUsed[index] == SW_FALSE && listRemoved[index] == SW_TRUE ) ? SW_TRUE : SW_FALSE;
                }
                // 뺀 키트에 기대는 것(남은 키트 · 에디터 확장)도 뺀다 — 남기면 resolve 가 없는 의존으로 멈춘다(CMake 는 그 매니페스트를 복사하지 않는다).
                bool bChanged = true;
                while ( bChanged )
                {
                    bChanged = false;
                    for ( size_t index = 0; index < manifestCount; ++index )
                    {
                        if ( listRemoved[index] == SW_TRUE || listUsed[index] == SW_TRUE )
                            continue;
                        for ( const ModuleDependency& dependency : inoutListManifest[index]._listDependency )
                        {
                            const size_t dependencyIndex = findIndex( inoutListManifest, dependency._name );
                            if ( dependencyIndex != kNotFound && listRemoved[dependencyIndex] == SW_TRUE )
                            {
                                listRemoved[index] = SW_TRUE;
                                bChanged           = true;
                                break;
                            }
                        }
                    }
                }
                vector<ModuleManifest> listKept;
                for ( size_t index = 0; index < manifestCount; ++index )
                {
                    if ( listRemoved[index] == SW_TRUE )
                        outListRemoved.push_back( ModuleInactiveEntry{ inoutListManifest[index]._name, "not used by " + gameName } );
                    else
                        listKept.push_back( std::move( inoutListManifest[index] ) );
                }
                inoutListManifest = std::move( listKept );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModuleCatalogLoader::loadAndResolve( uint8 targetMask, ModuleCatalog& outCatalog, ModuleResolution& outResolution )
    {
        // 무엇을 올릴지는 모듈 매니페스트가 정한다 — 빌드가 실행 파일 옆 `Modules/` 에 복사해 둔 것을 CMake 와 같은 규칙으로 해석한다.
        const string           catalogDirectory = ModuleImageUtil::getModuleDirectory();
        vector<ModuleManifest> listManifest;
        if ( FileUtil::isDirectory( catalogDirectory ) == false )
        {
            SW_LOG_ERROR( "Module catalog: Module catalog folder does not exist: %#", catalogDirectory.c_str() );
            return false;
        }
        if ( ModuleCatalogLoaderInternal::readManifestFolder( catalogDirectory, false, listManifest ) == false )
            return false;

        // 함께 지은 게임 팩(`Bin/Games/<게임>/`, `SW_GAMES`)을 고르면(`GameSelection`) 그 매니페스트가 `Modules/` 의 프로젝트를 대신한다 — 켜짐 표가 게임별이다.
        // 고르지 않은 게임(활성 게임 포함)의 매니페스트는 그 게임만 닿는 키트를 빼는 데만 쓴다.
        vector<ModuleManifest> listGamePackProject;
        if ( ModuleCatalogLoaderInternal::readManifestFolder( FileUtil::joinPath( FileUtil::getBinaryDirectory(), config::kDirBinGames ), true, listGamePackProject ) == false )
            return false;
        const size_t projectIndex = ModuleCatalogLoaderInternal::findIndex( listManifest, config::kTargetGameModule );
        string       movedModule;
        string       movedDirectory;
        if ( ModuleImageUtil::getModuleSubdirectory( movedModule, movedDirectory ) && projectIndex != ModuleCatalogLoaderInternal::kNotFound )
        {
            const string selectedPath =
                FileUtil::normalizePath( FileUtil::joinPath( FileUtil::joinPath( FileUtil::getBinaryDirectory(), movedDirectory ), movedModule + ModuleCatalog::kManifestExtension ) );
            bool bFound = false;
            for ( ModuleManifest& project : listGamePackProject )
            {
                if ( FileUtil::normalizePath( project._sourcePath ) != selectedPath )
                    continue;
                std::swap( project, listManifest[projectIndex] );
                bFound = true;
                break;
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR( "Module catalog: cannot read %#", selectedPath.c_str() );
                return false;
            }
        }
        vector<ModuleInactiveEntry> listRemoved;
        ModuleCatalogLoaderInternal::removeUnreachedGamePackKits( listManifest, listGamePackProject, GameSelection::getName(), listRemoved );

        string moduleError;
        for ( ModuleManifest& manifest : listManifest )
        {
            if ( outCatalog.addManifest( std::move( manifest ), moduleError ) == false )
            {
                SW_LOG_ERROR( "Module catalog: %#", moduleError.c_str() );
                return false;
            }
        }
        ModuleResolveContext resolveContext{};
        resolveContext._platform      = ModuleCatalog::getCurrentPlatform();
        resolveContext._configuration = ModuleCatalog::getCurrentConfiguration();
        resolveContext._targetMask    = targetMask;
        if ( outCatalog.resolve( resolveContext, outResolution, moduleError ) == false )
        {
            SW_LOG_ERROR( "Module manifests: %#", moduleError.c_str() );
            return false;
        }
        for ( ModuleInactiveEntry& removed : listRemoved )
        {
            outResolution._listInactive.push_back( std::move( removed ) );
        }
        std::sort( outResolution._listInactive.begin(), outResolution._listInactive.end(), []( const ModuleInactiveEntry& lhs, const ModuleInactiveEntry& rhs )
        { return lhs._name < rhs._name; } );
        SW_LOG_INFO( "Modules: %# active, %# off", outResolution._listLoadOrder.size(), outResolution._listInactive.size() );
        for ( [[maybe_unused]] const ModuleInactiveEntry& inactive : outResolution._listInactive ) // Shipping 은 로그가 빠진다
        {
            SW_LOG_INFO( "Module %# is off — %#", inactive._name.c_str(), inactive._reason.c_str() );
        }
        return true;
    }
} // namespace sw
