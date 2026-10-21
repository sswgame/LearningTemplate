#include "pch.h"

#include "Engine/Resource/Cook/PackCooker.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Resource/Cook/PackConfig.h"
#include "Engine/Resource/Pack/ResourcePakWriter.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "sw/config/CookContract.gen.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "PackCooker" );

    namespace
    {
        struct PackCookerInternal
        {
            /** @brief 쿠킹 표의 셰이더 바이너리 폴더(백엔드마다 하나)입니다. 담을 폴더가 아닌 것은 팩에서 뺀다. */
            static constexpr const utf8* kArrShaderFolder[] = {
#define SW_PACK_COOKER_SHADER_FOLDER_ROW( Backend, ShaderFolder, ... ) ShaderFolder,
                SW_RHI_BACKEND_TABLE( SW_PACK_COOKER_SHADER_FOLDER_ROW )
#undef SW_PACK_COOKER_SHADER_FOLDER_ROW
            };

            /** @brief GUID 레지스트리 파일 이름입니다(`AssetDatabase::writeRegistryFiles` 가 스테이징에 쓴다). */
            static constexpr string_view kAssetRegistryFileName{ "assetregistry.txt" };

            /** @brief 정규화(소문자 · '/')한 도메인 기준 경로를 폴더 성분과 파일 이름으로 나눕니다. */
            static void splitPath( string_view normalized, vector<string_view>& outListDirectory, string_view& outFileName )
            {
                outListDirectory.clear();
                size_t start = 0;
                while ( true )
                {
                    const size_t slash = normalized.find( '/', start );
                    if ( slash == string_view::npos )
                    {
                        outFileName = normalized.substr( start );
                        return;
                    }
                    if ( slash > start )
                        outListDirectory.push_back( normalized.substr( start, slash - start ) );
                    start = slash + 1;
                }
            }

            /** @brief 폴더 성분 가운데 @p name 이 있으면 true 입니다(대소문자 무시). */
            static bool containsDirectory( const vector<string_view>& listDirectory, string_view name )
            {
                for ( const string_view directory : listDirectory )
                {
                    if ( StringUtil::equals( directory, name, true ) )
                        return true;
                }
                return false;
            }

            /** @brief 패턴 가운데 하나가 파일 이름이나 도메인 기준 경로에 맞으면 true 입니다. */
            static bool matchesAnyPattern( const vector<string>& listPattern, string_view fileName, string_view normalized )
            {
                for ( const string& pattern : listPattern )
                {
                    if ( StringUtil::matchesWildcard( pattern, fileName ) || StringUtil::matchesWildcard( pattern, normalized ) )
                        return true;
                }
                return false;
            }

            /** @brief @p absPath 의 @p rootPath 기준 상대 경로('/')입니다. 루트 밖이면 빈 글입니다. */
            static string makeRelativePath( string_view rootPath, string_view absPath )
            {
                const string root       = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( rootPath ) );
                const string path       = FileUtil::normalizeSeparators( absPath );
                const bool   bUnderRoot = path.size() > root.size() + 1 && path[root.size()] == '/' && StringUtil::startsWith( path, root, true );
                return bUnderRoot ? path.substr( root.size() + 1 ) : string{};
            }

            /** @brief 굽을 도메인(`engine` · `common` · `game/<팩>`)을 모읍니다. 고른 게임 팩 폴더가 없으면 오류를 남기고 false 입니다. */
            static bool collectDomains( const PackCookParams& params, vector<string>& outListDomain )
            {
                outListDomain.clear();
                for ( const utf8* pShared : { "engine", "common" } )
                {
                    if ( FileUtil::isDirectory( FileUtil::joinPath( params._resourceRoot, pShared ) ) )
                        outListDomain.emplace_back( pShared );
                }
                const string gameRoot = FileUtil::joinPath( params._resourceRoot, path::kGamePack );
                if ( params._gamePackFolder.empty() == false )
                {
                    if ( FileUtil::isDirectory( FileUtil::joinPath( gameRoot, params._gamePackFolder ) ) == false )
                    {
                        SW_LOG_ERROR( "Game pack folder '%#' does not exist under %#", params._gamePackFolder, gameRoot );
                        return false;
                    }
                    outListDomain.push_back( string( path::kGamePack ) + "/" + params._gamePackFolder );
                    return true;
                }
                vector<string> listGameFolder;
                (void)FileUtil::collectFolders( gameRoot, listGameFolder, false ); // 게임 폴더가 없으면 공용 팩만 굽는다
                std::sort( listGameFolder.begin(), listGameFolder.end() );
                for ( const string& gameFolder : listGameFolder )
                {
                    outListDomain.push_back( string( path::kGamePack ) + "/" + FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameFolder ) ) );
                }
                return true;
            }

            /** @brief 작업 키의 설정 해시입니다 — 담는 규칙과 팩 바이트를 정하는 값 모두(설정 전체 · 셰이더 폴더 · 빌드 타깃). */
            static uint64 computeSettingsHash( const PackConfig& config, const PackCookParams& params )
            {
                string canonical = JSONSerializer::serialize( &config, *PackConfig::StaticType() );
                canonical.append( "\nshader=" ).append( params._shaderFolder ).append( "\ntarget=" ).append( params._buildTarget );
                return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
            }

            /**
             * @brief 파일 하나를 읽어 팩에 담고 그 내용을 작업의 입력으로 기록합니다.
             * @details 쿠커는 `ResourceIO` 가 아니라 디스크를 읽는다(스테이징은 리소스 루트 밖이고, 팩은 마운트된 팩이 아니라 느슨한 트리에서 굽는다) —
             *          그래서 입력을 손으로 기록한다. 기록하지 않으면 그 파일이 바뀌어도 팩을 다시 굽지 않는다.
             */
            static bool addFile( ResourcePakWriter& writer, string_view relativePath, const string& absPath )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( absPath, bytes ) == false )
                {
                    SW_LOG_ERROR( "Pack cook: could not read '%#'", absPath );
                    return false;
                }
                CookJob* pJob = CookJob::findCurrent();
                if ( pJob != nullptr )
                    pJob->recordInput( absPath, ContentHash::computeBytes( bytes ) );
                writer.addBytes( relativePath, std::move( bytes ) );
                return true;
            }

            /** @brief 도메인 하나의 팩을 @p outputPath 에 씁니다. 작업 범위 안에서 부른다. */
            static CookStep cookDomainPack( const PackConfig& config, const PackCookParams& params, const string& domain, const string& outputPath )
            {
                const string domainDir = FileUtil::joinPath( params._resourceRoot, domain );
                const string stagedDir = FileUtil::joinPath( params._cookedDir, domain );

                ResourcePakWriterDesc writerDesc;
                writerDesc._compression      = config._compression._codec;
                writerDesc._compressionLevel = config._compression._level;
                ResourcePakWriter writer( writerDesc );

                // 목록도 입력이다 — 파일이 생기거나 사라지면 다시 굽는다(`CookJob::collectFiles` 가 기록한다).
                vector<string> listSourceFile;
                CookJob::collectFiles( domainDir, "", listSourceFile );
                vector<string> listStale;
                bool           bHasMeta = false;
                for ( const string& absPath : listSourceFile )
                {
                    const string relativePath = makeRelativePath( domainDir, absPath );
                    if ( relativePath.empty() )
                        continue;
                    bHasMeta = bHasMeta || StringUtil::endsWith( relativePath, ".meta", true );
                    if ( PackCooker::shouldIncludeFile( relativePath, config, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    // 산출물은 스테이징에만 있다. 소스 트리에 남은 것은 낡은 쿠킹 잔재이고, Dev 런타임이 소스가 없을 때 그것으로 물러나 실패를 가리므로 넣지 않는다.
                    if ( AssetCookPath::isCookedOutput( relativePath ) )
                    {
                        listStale.push_back( relativePath );
                        continue;
                    }
                    if ( addFile( writer, relativePath, absPath ) == false )
                        return CookStep::Failed;
                }
                if ( listStale.empty() == false )
                    SW_LOG_WARNING( "Pack cook: %# has %# stale cooked files in the source tree (first: %#) - delete them; they are not packed", domain, listStale.size(),
                                    listStale.front() );

                vector<string> listStagedFile;
                CookJob::collectFiles( stagedDir, "", listStagedFile );
                bool bHasRegistry = false;
                for ( const string& absPath : listStagedFile )
                {
                    const string relativePath = makeRelativePath( stagedDir, absPath );
                    if ( relativePath.empty() )
                        continue;
                    bHasRegistry = bHasRegistry || StringUtil::equals( relativePath, kAssetRegistryFileName, true );
                    if ( addFile( writer, relativePath, absPath ) == false )
                        return CookStep::Failed;
                }

                // 레지스트리가 없으면 배포본의 GUID 참조가 전부 경로로 대신 찾는다(이름을 바꾼 에셋은 배포본에서만 못 찾는다) — 이 팩을 실패로 한다.
                if ( bHasMeta && bHasRegistry == false )
                {
                    SW_LOG_ERROR( "Pack cook: %# has .meta files but the staging folder has no %# - run the scene cook (--cook-scenes or --cook-all) first", domain,
                                  kAssetRegistryFileName );
                    return CookStep::Failed;
                }

                if ( writer.writePackFile( outputPath ) == false )
                    return CookStep::Failed;
                SW_LOG_INFO( "Pack cook: wrote %# (%# files)", outputPath, writer.getEntryCount() );
                return CookStep::Written;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PackCooker::makePackFileName( string_view domain )
    {
        string name = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( domain ) );
        std::replace( name.begin(), name.end(), '/', '_' );
        name += ".pack";
        return name;
    }

    bool PackCooker::shouldIncludeFile( string_view relativePath, const PackConfig& config, string_view shaderFolder, string_view buildTarget )
    {
        // 그 타깃의 런타임은 같은 표로 이 종류를 읽지 않는다(`ResourcePaths::setHostTarget`).
        if ( ResourcePaths::isExcludedForTarget( buildTarget, relativePath ) )
            return false;

        const string        normalized = FileUtil::normalizePath( relativePath );
        vector<string_view> listDirectory;
        string_view         fileName;
        PackCookerInternal::splitPath( normalized, listDirectory, fileName );

        for ( const string& directory : config._listGlobalExcludeDirectory )
        {
            if ( PackCookerInternal::containsDirectory( listDirectory, directory ) )
                return false;
        }
        if ( PackCookerInternal::matchesAnyPattern( config._listGlobalExcludePattern, fileName, normalized ) )
            return false;

        for ( const PackExcludeRule& rule : config._listRule )
        {
            for ( const string& directory : rule._listExcludeDirectory )
            {
                const bool bTopMatches = listDirectory.empty() == false && StringUtil::equals( listDirectory.front(), directory, true );
                const bool bExcluded   = rule._bRecursive ? PackCookerInternal::containsDirectory( listDirectory, directory ) : bTopMatches;
                if ( bExcluded )
                    return false;
            }
            if ( PackCookerInternal::matchesAnyPattern( rule._listExcludePattern, fileName, normalized ) )
                return false;
        }

        const bool bRawShaderSource = StringUtil::endsWith( fileName, ".hlsl", true ) || StringUtil::endsWith( fileName, ".hlsli", true );
        if ( config._bExcludeRawShaderSource && bRawShaderSource )
            return false;

        // 다른 백엔드의 셰이더 바이너리는 이 팩의 런타임이 읽지 않는다.
        if ( StringUtil::contains( normalized, "shaders/bin", true ) )
        {
            for ( const utf8* pShaderFolder : PackCookerInternal::kArrShaderFolder )
            {
                const bool bOtherBackend = StringUtil::equals( pShaderFolder, shaderFolder, true ) == false && PackCookerInternal::containsDirectory( listDirectory, pShaderFolder );
                if ( bOtherBackend )
                    return false;
            }
        }
        return true;
    }

    bool PackCooker::cookAllPacks( const PackConfig& config, const PackCookParams& params, const CookStore& store, uint32& outPackCount )
    {
        outPackCount             = 0;
        const bool bMissingParam = params._resourceRoot.empty() || params._cookedDir.empty() || params._packOutputDir.empty() || params._shaderFolder.empty();
        if ( bMissingParam )
        {
            SW_LOG_ERROR( "Pack cook needs the resource root, --cooked-dir, --pack-dir and a shader folder" );
            return false;
        }

        vector<string> listDomain;
        if ( PackCookerInternal::collectDomains( params, listDomain ) == false )
            return false;
        if ( listDomain.empty() )
        {
            SW_LOG_ERROR( "Pack cook found no domain folder under %#", params._resourceRoot );
            return false;
        }

        CookJobDesc desc;
        desc._cookerName    = "Pack";
        desc._cookerVersion = kCookerVersion;
        desc._target        = params._buildTarget + "-" + params._shaderFolder;
        desc._settingsHash  = PackCookerInternal::computeSettingsHash( config, params );

        uint32 failedCount = 0;
        for ( const string& domain : listDomain )
        {
            const string packName   = makePackFileName( domain );
            const string outputPath = FileUtil::joinPath( params._packOutputDir, packName );
            // 기록은 타깃 · 백엔드마다 따로 둔다 — 같은 팩 이름을 다른 설정으로 번갈아 구워도 서로의 기록을 덮지 않는다.
            const string      outputID = FileUtil::joinPath( FileUtil::joinPath( "packs", desc._target ), packName );
            const CookOutcome outcome  = store.cookFile( desc, outputID, outputPath, [&]()
             { return PackCookerInternal::cookDomainPack( config, params, domain, outputPath ); } );
            if ( outcome == CookOutcome::Failed )
                ++failedCount;
            else if ( outcome != CookOutcome::Skipped )
                ++outPackCount;
        }
        SW_LOG_INFO( "Pack cook: %# packs written or reused, %# failed (target %#)", outPackCount, failedCount, desc._target );
        return failedCount == 0;
    }
} // namespace sw
