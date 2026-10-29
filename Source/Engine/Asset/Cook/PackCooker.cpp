#include "pch.h"

#include "Engine/Asset/Cook/PackCooker.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/ContentHash.h"
#include "Engine/FileSystem/Pack/PackConfig.h"
#include "Engine/FileSystem/Pack/PackWriter.h"
#include "Engine/Foundation/EngineDefines.h"
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
            /** @brief 팩 쿠킹 전체의 단계별 시간입니다(요약 로그). 팩 여럿이 더해 가고, 단계는 한 스레드에서 차례로 돌므로 합이 곧 그 단계에 쓴 시간이다. */
            struct PackCookTiming
            {
                PackWriteStats _write;               ///< 압축 · 배치 · 파일 쓰기(`PackWriter`)
                int64          _readNanos{ 0 };      ///< 원본 · 스테이징 파일 읽기
                int64          _inputHashNanos{ 0 }; ///< 입력 내용 해시(`addFile`) — 출력 해시는 `CookStore::getOutputHashNanos`
            };

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
            /** @brief @p normalized 가 @p shaderFolder 가 아닌 백엔드의 셰이더 바이너리(`shaders/bin/<다른 폴더>`)면 true 입니다 — 이 팩의 런타임이 읽지 않는다. */
            static bool isOtherBackendShaderBinary( string_view normalized, const vector<string_view>& listDirectory, string_view shaderFolder );

            static bool containsDirectory( const vector<string_view>& listDirectory, string_view name )
            {
                for ( const string_view directory : listDirectory )
                {
                    if ( StringUtil::equals( directory, name, true ) )
                        return true;
                }
                return false;
            }

            /** @brief 패턴 가운데 하나가 도메인 기준 경로에 맞으면 true 입니다(`/` 없는 패턴은 파일 이름에, 있으면 마디별로: `StringUtil::matchesPathPattern`). */
            static bool matchesAnyPattern( const vector<string>& listPattern, string_view normalized )
            {
                for ( const string& pattern : listPattern )
                {
                    if ( StringUtil::matchesPathPattern( pattern, normalized ) )
                        return true;
                }
                return false;
            }

            /** @brief 굽을 도메인(`engine` · `common` · `game/<팩>`)을 모읍니다. 고른 게임 팩 폴더가 없으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool collectDomains( const PackCookParams& params, vector<string>& outListDomain )
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
                // 게임 폴더가 없으면 공용 팩만 베이크한다. 도중에 멈춘 목록이면 빠진 게임 팩을 모른 채 넘어가므로 실패다(경로는 collectFolders 가 알린다).
                if ( FileUtil::isDirectory( gameRoot ) && FileUtil::collectFolders( gameRoot, listGameFolder, false ) == false )
                    return false;
                VectorUtil::sort( listGameFolder );
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
                for ( const string& excludedPath : params._listExcludedSourceFile )
                {
                    canonical.append( "\nexclude=" ).append( excludedPath );
                }
                return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
            }

            /**
             * @brief 파일 하나를 읽어 팩에 담고 그 내용을 작업의 입력으로 기록합니다.
             * @details 쿠커는 `AssetIO` 가 아니라 디스크를 읽는다(스테이징은 리소스 루트 밖이고, 팩은 마운트된 팩이 아니라 느슨한 트리에서 베이크한다) —
             *          그래서 입력을 손으로 기록한다. 기록하지 않으면 그 파일이 바뀌어도 팩을 다시 베이크하지 않는다.
             */
            static bool addFile( PackWriter& writer, string_view relativePath, const string& absPath, PackCookTiming& ioTiming )
            {
                vector<uint8>   bytes;
                const Stopwatch readWatch;
                const bool      bRead = FileUtil::readFile( absPath, bytes );
                ioTiming._readNanos += readWatch.getElapsedNanoseconds();
                if ( bRead == false )
                {
                    SW_LOG_ERROR( "Pack cook: could not read '%#'", absPath );
                    return false;
                }
                CookJob* pJob = CookJob::findCurrent();
                if ( pJob != nullptr )
                {
                    const Stopwatch hashWatch;
                    const uint64    hash = ContentHash::computeBytes( bytes );
                    ioTiming._inputHashNanos += hashWatch.getElapsedNanoseconds();
                    pJob->recordInput( absPath, hash );
                }
                writer.addBytes( relativePath, std::move( bytes ) );
                return true;
            }

            /**
             * @brief 팩 쿠킹 요약 한 줄을 남깁니다 — 단계별 ms 로 병렬 압축의 효과를 잰다.
             * @details `compress` 는 `벽시계 / CPU 합` 이다(CPU 합이 직렬로 돌았다면 든 시간이라 둘의 비가 병렬 효과). `hash` 는 입력 내용 해시(`addFile`)와 출력 내용 해시(`CookStore` 의 재사용 판정 · 기록)에 쓴 시간이고,
             *          바뀐 것 없는 실행에서는 출력 해시가 거의 전부다. `other` 는 총 시간에서 나머지 단계를 뺀 몫이다 — 출력 읽기 · DDC 저장 · 기록 쓰기 · 폴더 훑기.
             */
            static void logSummary( uint32 packCount, uint32 failedCount, string_view target, int64 totalNanos, int64 hashNanos, const PackCookTiming& timing )
            {
                const PackWriteStats& write      = timing._write;
                const int64           otherNanos = totalNanos - ( timing._readNanos + write._compressWallNanos + write._layoutNanos + hashNanos + write._writeNanos );
                SW_LOG_INFO( "Pack cook: %# packs written or reused, %# failed (target %#) in %# ms - read %# ms, compress %# ms wall / %# ms cpu, layout %# ms, hash %# ms, write %# ms, other %# ms (%# files, %# bytes)",
                             packCount, failedCount, target, totalNanos / constant::kNanosecondsPerMillisecond, timing._readNanos / constant::kNanosecondsPerMillisecond,
                             write._compressWallNanos / constant::kNanosecondsPerMillisecond, write._compressCPUNanos / constant::kNanosecondsPerMillisecond,
                             write._layoutNanos / constant::kNanosecondsPerMillisecond, hashNanos / constant::kNanosecondsPerMillisecond,
                             write._writeNanos / constant::kNanosecondsPerMillisecond, otherNanos / constant::kNanosecondsPerMillisecond, write._entryCount, write._inputBytes );
            }

            /** @brief 도메인 하나의 팩을 @p outputPath 에 씁니다. 작업 범위 안에서 부른다. 단계별 시간은 @p ioTiming 에 더한다. */
            static CookStep cookDomainPack( const PackConfig& config, const PackCookParams& params, const string& domain, const string& outputPath, PackCookTiming& ioTiming )
            {
                const string domainDir = FileUtil::joinPath( params._resourceRoot, domain );
                const string stagedDir = FileUtil::joinPath( params._cookedDir, domain );

                PackWriterDesc writerDesc;
                writerDesc._compression      = config._compression._codec;
                writerDesc._compressionLevel = config._compression._level;
                writerDesc._layoutMode       = config._layout._mode;
                writerDesc._maxHolePercent   = static_cast<uint32>( config._layout._maxHolePercent );
                writerDesc._previousPackPath = outputPath; // 덮어쓸 팩이 앞 빌드다(`Slot` 이 안 바뀐 항목의 자리를 지킨다). 없으면 첫 배치다
                PackWriter writer( writerDesc );

                // 목록도 입력이다 — 파일이 생기거나 사라지면 다시 베이크한다(`CookJob::collectFiles` 가 기록한다).
                vector<string> listSourceFile;
                if ( CookJob::collectFiles( domainDir, "", listSourceFile ) == false )
                    return CookStep::Failed;
                vector<string> listStale;
                bool           bHasMeta = false;
                for ( const string& absPath : listSourceFile )
                {
                    string relativePath;
                    if ( FileUtil::tryMakePathUnderRoot( domainDir, absPath, relativePath, true ) == false )
                        continue;
                    bHasMeta = bHasMeta || StringUtil::endsWith( relativePath, ".meta", true );
                    if ( PackCooker::shouldIncludeFile( relativePath, config, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    if ( VectorUtil::contains( params._listExcludedSourceFile, FileUtil::normalizePath( absPath ) ) )
                        continue; // 위 층이 뺀 파일(출시 문화권 밖 번역 테이블 — 언리얼 Cultures to Stage). 런타임은 없는 테이블을 "아직 번역 없음" 으로 읽는다
                    // 산출물은 스테이징에만 있다. 소스 트리에 남은 것은 낡은 쿠킹 잔재이고, Dev 런타임이 소스가 없을 때 그것으로 물러나 실패를 가리므로 넣지 않는다.
                    if ( AssetCookPath::isCookedOutput( relativePath ) )
                    {
                        listStale.push_back( relativePath );
                        continue;
                    }
                    if ( addFile( writer, relativePath, absPath, ioTiming ) == false )
                        return CookStep::Failed;
                }
                if ( listStale.empty() == false )
                    SW_LOG_WARNING( "Pack cook: %# has %# stale cooked files in the source tree (first: %#) - delete them; they are not packed", domain, listStale.size(),
                                    listStale.front() );

                vector<string> listStagedFile;
                if ( CookJob::collectFiles( stagedDir, "", listStagedFile ) == false )
                    return CookStep::Failed;
                bool bHasRegistry = false;
                for ( const string& absPath : listStagedFile )
                {
                    string relativePath;
                    if ( FileUtil::tryMakePathUnderRoot( stagedDir, absPath, relativePath, true ) == false )
                        continue;
                    bHasRegistry = bHasRegistry || StringUtil::equals( relativePath, kAssetRegistryFileName, true );
                    if ( PackCooker::shouldIncludeStagedFile( relativePath, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    if ( addFile( writer, relativePath, absPath, ioTiming ) == false )
                        return CookStep::Failed;
                }

                // 레지스트리가 없으면 배포본의 GUID 참조가 전부 경로로 대신 찾는다(이름을 바꾼 에셋은 배포본에서만 못 찾는다) — 이 팩을 실패로 한다.
                if ( bHasMeta && bHasRegistry == false )
                {
                    SW_LOG_ERROR( "Pack cook: %# has .meta files but the staging folder has no %# - run the scene cook (--cook-scenes or --cook-all) first", domain,
                                  kAssetRegistryFileName );
                    return CookStep::Failed;
                }

                PackWriteStats writeStats;
                if ( writer.writePackFile( outputPath, &writeStats ) == false )
                    return CookStep::Failed;
                ioTiming._write.add( writeStats );
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
        // 그 타깃의 런타임은 같은 표로 이 종류를 읽지 않는다(`AssetPaths::setHostTarget`).
        if ( AssetPaths::isExcludedForTarget( buildTarget, relativePath ) )
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
        if ( PackCookerInternal::matchesAnyPattern( config._listGlobalExcludePattern, normalized ) )
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
            if ( PackCookerInternal::matchesAnyPattern( rule._listExcludePattern, normalized ) )
                return false;
        }

        const bool bRawShaderSource = StringUtil::endsWith( fileName, ".hlsl", true ) || StringUtil::endsWith( fileName, ".hlsli", true );
        if ( config._bExcludeRawShaderSource && bRawShaderSource )
            return false;

        return PackCookerInternal::isOtherBackendShaderBinary( normalized, listDirectory, shaderFolder ) == false;
    }

    bool PackCooker::shouldIncludeStagedFile( string_view relativePath, string_view shaderFolder, string_view buildTarget )
    {
        if ( AssetPaths::isExcludedForTarget( buildTarget, relativePath ) )
            return false;
        const string        normalized = FileUtil::normalizePath( relativePath );
        vector<string_view> listDirectory;
        string_view         fileName;
        PackCookerInternal::splitPath( normalized, listDirectory, fileName );
        return PackCookerInternal::isOtherBackendShaderBinary( normalized, listDirectory, shaderFolder ) == false;
    }

    bool PackCookerInternal::isOtherBackendShaderBinary( string_view normalized, const vector<string_view>& listDirectory, string_view shaderFolder )
    {
        if ( StringUtil::contains( normalized, "shaders/bin", true ) == false )
            return false;
        for ( const utf8* pShaderFolder : kArrShaderFolder )
        {
            if ( StringUtil::equals( pShaderFolder, shaderFolder, true ) == false && containsDirectory( listDirectory, pShaderFolder ) )
                return true;
        }
        return false;
    }

    bool PackCooker::cookAllPacks( const PackConfig& config, const PackCookParams& params, const CookStore& store, uint32& outPackCount )
    {
        outPackCount             = 0;
        const bool bMissingParam = params._resourceRoot.empty() || params._cookedDir.empty() || params._packOutputDir.empty() || params._shaderFolder.empty();
        if ( bMissingParam )
        {
            SW_LOG_ERROR( "Pack cook needs the content root, --cooked-dir, --pack-dir and a shader folder" );
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

        uint32                             failedCount = 0;
        PackCookerInternal::PackCookTiming timing;
        const Stopwatch                    totalWatch;
        const int64                        outputHashStart = store.getOutputHashNanos(); // 이 쿠킹의 몫만 센다
        for ( const string& domain : listDomain )
        {
            const string packName   = makePackFileName( domain );
            const string outputPath = FileUtil::joinPath( params._packOutputDir, packName );
            // 기록은 타깃 · 백엔드마다 따로 둔다 — 같은 팩 이름을 다른 설정으로 번갈아 베이크해도 서로의 기록을 덮지 않는다.
            const string      outputID = FileUtil::joinPath( FileUtil::joinPath( "packs", desc._target ), packName );
            const CookOutcome outcome  = store.cookFile( desc, outputID, outputPath, [&]()
             { return PackCookerInternal::cookDomainPack( config, params, domain, outputPath, timing ); } );
            if ( outcome == CookOutcome::Failed )
                ++failedCount;
            else if ( outcome != CookOutcome::Skipped )
                ++outPackCount;
        }
        PackCookerInternal::logSummary( outPackCount, failedCount, desc._target, totalWatch.getElapsedNanoseconds(),
                                        timing._inputHashNanos + store.getOutputHashNanos() - outputHashStart, timing );
        return failedCount == 0;
    }
} // namespace sw
