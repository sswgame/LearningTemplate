#include "pch.h"

#include "Engine/Asset/Cook/PackCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Asset/Cook/PackReachResult.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackTypes.h"
#include "Engine/Content/Pack/PackWriter.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "sw/config/CookContract.gen.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PackCookerInternal
        {
            /** @brief 팩에서 뺀 셰이더 바이너리 가운데 로그 줄에 이름을 적는 수입니다. */
            static constexpr uint32 kDroppedShaderNameCount = 5;

            /** @brief 팩에서 뺀 셰이더 바이너리의 수와 처음 몇 이름입니다(로그 한 줄). */
            struct DroppedShaderList
            {
                uint32 _count{ 0 };
                string _names{};

                void add( const string& relativePath )
                {
                    if ( _count < kDroppedShaderNameCount )
                        _names += ( _count == 0 ? "" : ", " ) + relativePath;
                    ++_count;
                }
            };

            /** @brief 팩 쿠킹 전체의 단계별 시간입니다(요약 로그). 팩 여럿이 더해 가고, 단계는 한 스레드에서 차례로 돌므로 합이 곧 그 단계에 쓴 시간이다. */
            struct PackCookTiming
            {
                PackWriteStats _write;          ///< 압축 · 배치 · 파일 쓰기(`PackWriter`)
                int64          _readNanos{ 0 }; ///< 원본 · 스테이징 파일 읽기
            };

            /** @brief 고르기 결과입니다 — 담을 원본 id, 닿았지만 제외 규칙이 뺀 원본 id(모두 정규화한 리소스 id), 스테이징 산출물의 고르기입니다. */
            struct PackSelection
            {
                unordered_set<string> _setIncluded;
                unordered_set<string> _setReferencedExcluded;
                PackStagedSelection   _staged;
            };

            /** @brief 고르기가 글을 읽는 디스크 입구입니다. 읽기는 `AssetIO::readBinaryFile` 이라 고르기 범위(`PackReach`)가 받아 준다(그 범위의 기록은 쓰지 않는다). */
            class PackDiskReachSource final : public IPackReachSource
            {
            public:
                unordered_map<string, string> _mapAbsolutePath; ///< 리소스 id → 디스크 절대 경로(리눅스는 대소문자를 가린다 — 정규화한 id 로 열지 않는다)

                bool readText( const string& assetID, string& outText ) const override
                {
                    const auto itPath = _mapAbsolutePath.find( assetID );
                    if ( itPath == _mapAbsolutePath.end() )
                        return false;
                    vector<uint8> bytes;
                    if ( AssetIO::readBinaryFile( itPath->second, bytes ) == false )
                        return false;
                    outText = string( string_view( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ) );
                    return true;
                }
            };

            /** @brief 도메인 하나의 후보 파일(정규화 id · 절대 경로)입니다. 요약 로그가 담은 수 · 크기를 센다. */
            struct PackCandidateFile
            {
                string _assetID;
                string _absolutePath;
            };
            struct PackDomainCandidates
            {
                string                    _domain;
                vector<PackCandidateFile> _listFile;     ///< 담을 수 있는 원본
                vector<string>            _listExisting; ///< 있는 모든 원본(제외 규칙이 뺀 것 포함) — 스테이징 산출물의 원본 표를 만든다
            };

            /** @brief 원본 파일 하나가 팩에 담길 수 있는지(제외 규칙 · 위 층이 뺀 파일 · 낡은 쿠킹본이 아닌지)입니다. 고르기의 후보 판정이다. */
            static bool isPackableSource( string_view relativePath, const string& absPath, const PackConfig& config, const PackCookParams& params )
            {
                return PackCooker::shouldIncludeFile( relativePath, config, params._shaderFolder, params._buildTarget ) &&
                       VectorUtil::contains( params._listExcludedSourceFile, FileUtil::normalizePath( absPath ) ) == false && AssetCookPath::isCookedOutput( relativePath ) == false;
            }

            /** @brief 도메인 기준 상대 경로의 정규화 리소스 id(`game/empty/maps/a.scene.xml`)입니다. */
            static string makeAssetID( string_view domain, string_view relativePath ) { return FileUtil::normalizePath( FileUtil::joinPath( domain, relativePath ) ); }

            /** @brief 고르기 요약입니다 — 도메인마다 담은 파일 수 · 바이트와 후보 수 · 바이트, 그리고 합계(보고 · 크기 추적용). */
            static void logSelection( const vector<PackDomainCandidates>& listCandidates, const PackReachResult& reach )
            {
                uint64 totalIncludedCount = 0;
                uint64 totalCount         = 0;
                uint64 totalIncludedBytes = 0;
                uint64 totalBytes         = 0;
                for ( const PackDomainCandidates& candidates : listCandidates )
                {
                    uint64 includedCount = 0;
                    uint64 includedBytes = 0;
                    uint64 bytes         = 0;
                    for ( const PackCandidateFile& file : candidates._listFile )
                    {
                        uint64 size = 0;
                        (void)FileUtil::tryGetFileSize( file._absolutePath, size ); // 크기는 요약에만 쓴다 — 읽지 못하는 파일은 팩 쓰기가 오류로 멈춘다
                        bytes += size;
                        if ( reach._setIncluded.contains( file._assetID ) == false )
                            continue;
                        ++includedCount;
                        includedBytes += size;
                    }
                    SW_LOG_INFO_ID( "PackCooker.PackReachDomain", "Pack cook reach: %# packs %# of %# files (%# of %# bytes)", candidates._domain, includedCount,
                                    static_cast<uint64>( candidates._listFile.size() ), includedBytes, bytes );
                    totalIncludedCount += includedCount;
                    totalCount += candidates._listFile.size();
                    totalIncludedBytes += includedBytes;
                    totalBytes += bytes;
                }
                SW_LOG_INFO_ID( "PackCooker.PackReachSummary", "Pack cook reach: %# roots, %# of %# files (%# of %# bytes) across %# domains", reach._rootCount, totalIncludedCount, totalCount,
                                totalIncludedBytes, totalBytes, static_cast<uint64>( listCandidates.size() ) );
            }

            /**
             * @brief 굽는 모든 도메인을 한 번에 보고 담을 파일을 고릅니다(`PackReachResult`). 빠진 참조가 있으면 오류를 남기고 false 입니다.
             * @details 작업 범위 하나(`PackReach`) 안에서 읽는다 — 쿠킹 세션은 작업 밖 읽기를 막는다. 그 범위의 입력은 기록하지 않는다: 고른 결과는 팩마다 설정 해시로 들어가고
             *          담긴 파일은 팩 작업이 다시 읽어 그 작업의 입력이 된다(`computeSelectionHash`).
             */
            [[nodiscard]] static bool selectFiles( const PackConfig& config, const PackCookParams& params, const vector<string>& listDomain, const CookJobDesc& desc, PackSelection& outSelection )
            {
                CookJobDesc reachDesc = desc;
                reachDesc._cookerName = "PackReach";
                const CookJob reachJob( reachDesc );

                // 작업 범위 안에서 읽는다 — 쿠킹 세션은 작업 밖 읽기를 막는다. 이 표가 바뀌면 고르는 파일도 바뀌므로 입력이다.
                AssetReferenceFields fields;
                if ( fields.load( params._projectRoot ) == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.ReferenceFieldsUnreadable", "Pack cook: the asset reference fields under %# could not be read - references cannot be followed", params._projectRoot );
                    return false;
                }

                PackReachFiles               files;
                PackDiskReachSource          source;
                vector<PackDomainCandidates> listCandidates;
                const string                 gamePrefix = string( constant::path::kGamePack ) + "/";
                for ( const string& domain : listDomain )
                {
                    const string   domainDir = FileUtil::joinPath( params._resourceRoot, domain );
                    vector<string> listFile;
                    if ( CookJob::collectFiles( domainDir, "", listFile ) == false )
                        return false;
                    PackDomainCandidates& candidates = listCandidates.emplace_back();
                    candidates._domain               = domain;
                    for ( const string& absPath : listFile )
                    {
                        string relativePath;
                        if ( FileUtil::tryMakePathUnderRoot( domainDir, absPath, relativePath ) == false )
                            continue;
                        string assetID = makeAssetID( domain, relativePath );
                        files._setExisting.insert( assetID );
                        candidates._listExisting.push_back( assetID );
                        if ( isPackableSource( relativePath, absPath, config, params ) == false )
                            continue;
                        files._setPackable.insert( assetID );
                        source._mapAbsolutePath.try_emplace( assetID, absPath );
                        candidates._listFile.push_back( PackCandidateFile{ std::move( assetID ), absPath } );
                    }
                    if ( StringUtil::startsWith( domain, gamePrefix ) )
                        files._listGameDomain.push_back( FileUtil::normalizePath( domain ) );
                }

                AssetCookRootRegistry::appendRoots( files._listCodeRoot );
                files._automation = params._automationContent;
                PackReachResult reach;
                const bool      bReached = PackReachResult::collect( fields, config._cookRules, files, source, reach );
                logSelection( listCandidates, reach );
                if ( bReached == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackReachFailed", "Pack cook: following references from the cook roots failed (reasons above) - no pack is written" );
                    return false;
                }
                vector<PackDomainSources> listSources;
                for ( const PackDomainCandidates& candidates : listCandidates )
                {
                    PackDomainSources& sources = listSources.emplace_back();
                    sources._domain            = candidates._domain;
                    sources._listExistingID    = candidates._listExisting;
                }
                outSelection._staged.build( listSources, reach );
                outSelection._setIncluded           = std::move( reach._setIncluded );
                outSelection._setReferencedExcluded = std::move( reach._setReferencedExcluded );
                return true;
            }

            /** @brief 도메인 팩 작업의 설정 해시입니다 — 쿠킹 설정 해시에 그 도메인에서 고른 파일 목록(정렬)을 섞는다. 다른 팩의 참조가 바뀌어 고른 것이 달라지면 다시 굽는다. */
            static uint64 computeSelectionHash( uint64 settingsHash, const string& domain, const PackSelection& selection )
            {
                const string   prefix = FileUtil::normalizePath( domain ) + "/";
                vector<string> listID;
                for ( const unordered_set<string>* pSet : { &selection._setIncluded, &selection._setReferencedExcluded } )
                {
                    for ( const string& assetID : *pSet )
                    {
                        if ( StringUtil::startsWith( assetID, prefix ) )
                            listID.push_back( assetID );
                    }
                }
                std::sort( listID.begin(), listID.end() );
                uint64 hash = settingsHash;
                for ( const string& assetID : listID )
                {
                    hash = HashUtil::combine( hash, HashUtil::computeStringHash64( assetID.c_str(), assetID.size(), false ) );
                }
                return hash;
            }

            /** @brief 쿠킹 표의 셰이더 바이너리 폴더(백엔드마다 하나)입니다. 담을 폴더가 아닌 것은 팩에서 뺀다. */
            static constexpr const utf8* kArrShaderFolder[] = {
#define SW_PACK_COOKER_SHADER_FOLDER_ROW( Backend, ShaderFolder, ... ) ShaderFolder,
                SW_RHI_BACKEND_TABLE( SW_PACK_COOKER_SHADER_FOLDER_ROW )
#undef SW_PACK_COOKER_SHADER_FOLDER_ROW
            };

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

            /** @brief @p normalized 가 @p shaderFolder 가 아닌 백엔드의 셰이더 바이너리(`shaders/bin/<다른 폴더>`)면 true 입니다 — 이 팩의 런타임이 읽지 않는다. */
            static bool isOtherBackendShaderBinary( string_view normalized, const vector<string_view>& listDirectory, string_view shaderFolder );

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
                const string gameRoot = FileUtil::joinPath( params._resourceRoot, constant::path::kGamePack );
                if ( params._gamePackFolder.empty() == false )
                {
                    if ( FileUtil::isDirectory( FileUtil::joinPath( gameRoot, params._gamePackFolder ) ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.GamePackFolderDoes", "Game pack folder '%#' does not exist under %#", params._gamePackFolder, gameRoot );
                        return false;
                    }
                    outListDomain.push_back( string( constant::path::kGamePack ) + "/" + params._gamePackFolder );
                    return true;
                }
                // 게임 폴더가 없으면 공용 팩만 베이크한다. 도중에 멈춘 목록이면 빠진 게임 팩을 모른 채 넘어가므로 실패다.
                return AssetPaths::collectGamePackDomains( params._resourceRoot, outListDomain );
            }

            /** @brief 작업 키의 설정 해시입니다 — 담는 규칙과 팩 바이트를 정하는 값 모두(설정 전체 · 셰이더 폴더 · 빌드 타깃). */
            static uint64 computeSettingsHash( const PackConfig& config, const PackCookParams& params )
            {
                string canonical = JSONSerializer::serialize( &config, *PackConfig::StaticType() );
                canonical.append( "\nshader=" ).append( params._shaderFolder ).append( "\ntarget=" ).append( params._buildTarget );
                // 팩 형식 버전도 설정이다 - 형식을 올리면 옛 형식의 산출물을 재사용하지 않는다(리더가 거절할 팩을 남기지 않게).
                utf8 arrFormat[constant::buffer::kMax32];
                formatstring( arrFormat, constant::buffer::kMax32, "\nformat=%#", kPackFormatVersion );
                canonical.append( arrFormat );
                for ( const string& excludedPath : params._listExcludedSourceFile )
                {
                    canonical.append( "\nexclude=" ).append( excludedPath );
                }
                return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
            }

            /**
             * @brief 파일 하나를 읽어 팩에 담습니다. 디스크 읽기 입구(`AssetIO::readBinaryFile`)가 읽기와 작업 입력 기록(`CookJob::allowDiskRead`)을 함께 한다.
             * @details 팩은 마운트된 팩이 아니라 느슨한 트리 · 스테이징(리소스 루트 밖)에서 베이크하므로 디스크 경로로 읽는다. 예전에는 `FileUtil` 로 읽고 입력을 손으로 기록했다(같은 일 둘).
             *          읽기 시간에는 입력 해시가 들어 있다.
             */
            static bool addFile( PackWriter& writer, string_view relativePath, const string& absPath, PackCookTiming& ioTiming )
            {
                vector<uint8>   bytes;
                const Stopwatch readWatch;
                const bool      bRead = AssetIO::readBinaryFile( absPath, bytes );
                ioTiming._readNanos += readWatch.getElapsedNanoseconds();
                if ( bRead == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackCookCouldNot", "Pack cook: could not read '%#'", absPath );
                    return false;
                }
                writer.addBytes( relativePath, std::move( bytes ) );
                return true;
            }

            /**
             * @brief 소스 파일 하나를 팩에 담습니다. 부모를 가리키는 데이터(`_base`)는 펼친 한 벌을 담습니다 — 런타임은 펼친 결과만 읽어 비용이 없다(`DataInheritance`).
             * @details 부모도 디스크 입구로 읽으므로 이 작업의 입력이 되어, 부모가 바뀌면 팩을 다시 굽는다. 스스로 쿠킹본을 만드는 종류(프리팹 변형 — `cookAllPrefabs`)는 그 쿠커가 펼쳤다.
             *          펼치지 못하면 쿠킹 실패다 — 부모 없이 자식 칸만 담지 않는다. 카탈로그 참조 규칙(@p references)이 있는 루트는 펼친 글로 참조를 보고, 필수 참조가 깨졌으면
             *          쿠킹 실패다 — 런타임 카탈로그 읽기와 같은 표 · 같은 검사라 실행해야 드러나던 깨진 id 가 팩에 담기지 않는다.
             */
            static bool addSourceFile( PackWriter& writer, string_view relativePath, const string& absPath, string_view contentRoot, const DataCatalogReferenceRules& references,
                                       PackCookTiming& ioTiming )
            {
                vector<uint8>   bytes;
                const Stopwatch readWatch;
                const bool      bRead = AssetIO::readBinaryFile( absPath, bytes );
                ioTiming._readNanos += readWatch.getElapsedNanoseconds();
                if ( bRead == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackCookCouldNot2", "Pack cook: could not read '%#'", absPath );
                    return false;
                }
                const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
                if ( AssetCookPath::isCookableSource( relativePath ) )
                {
                    writer.addBytes( relativePath, std::move( bytes ) );
                    return true;
                }
                DataInheritanceDiskSource source{ contentRoot };
                string                    flatText;
                bool                      bComposed = false;
                if ( ResourceComposer::compose( text, absPath, ResourceContext::forPackCook( source ), flatText, bComposed ) == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackCookCouldNotFlatten", "Pack cook: could not flatten '%#'", absPath );
                    return false;
                }
                const string_view packedText = bComposed ? string_view{ flatText } : text;
                if ( references.isRootMentioned( packedText ) )
                {
                    XMLDocument doc;
                    if ( doc.parse( packedText, absPath ) && doc.getRoot().isValid() && references.validateDocument( doc.getRoot(), absPath ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.PackCookBrokenReference", "Pack cook: '%#' names ids that are not in the file (rules in %#)", absPath, DataCatalogReferenceRules::kTableAssetID );
                        return false;
                    }
                }
                if ( bComposed )
                    writer.addBytes( relativePath, vector<uint8>( flatText.begin(), flatText.end() ) );
                else
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
                SW_LOG_INFO_ID( "PackCooker.PackCookPacksWritten", "Pack cook: %# packs written or reused, %# failed (target %#) in %# ms - read %# ms, compress %# ms wall / %# ms cpu, layout %# ms, hash %# ms, write %# ms, other %# ms (%# files, %# bytes)",
                                packCount, failedCount, target, totalNanos / constant::time::kNanosecondsPerMillisecond, timing._readNanos / constant::time::kNanosecondsPerMillisecond,
                                write._compressWallNanos / constant::time::kNanosecondsPerMillisecond, write._compressCPUNanos / constant::time::kNanosecondsPerMillisecond,
                                write._layoutNanos / constant::time::kNanosecondsPerMillisecond, hashNanos / constant::time::kNanosecondsPerMillisecond,
                                write._writeNanos / constant::time::kNanosecondsPerMillisecond, otherNanos / constant::time::kNanosecondsPerMillisecond, write._entryCount, write._inputBytes );
            }

            /** @brief 도메인 하나의 팩을 @p outputPath 에 씁니다. 작업 범위 안에서 부른다. 단계별 시간은 @p ioTiming 에 더한다. */
            static CookStep cookDomainPack( const PackConfig& config, const PackCookParams& params, const string& domain, const string& outputPath, const PackSelection& selection,
                                            PackCookTiming& ioTiming )
            {
                const string domainDir = FileUtil::joinPath( params._resourceRoot, domain );
                const string stagedDir = FileUtil::joinPath( params._cookedDir, domain );

                PackWriterDesc writerDesc;
                writerDesc._compression      = config._compression._codec;
                writerDesc._compressionLevel = config._compression._level;
                writerDesc._layoutMode       = config._layout._mode;
                writerDesc._maxHolePercent   = static_cast<uint32>( config._layout._maxHolePercent );
                writerDesc._previousPackPath = outputPath; // 덮어쓸 팩이 앞 빌드다(`Slot` 이 안 바뀐 항목의 자리를 지킨다). 없으면 첫 배치다
                writerDesc._mountDomain      = domain;
                PackWriter writer( writerDesc );

                // 카탈로그 참조 규칙 — 표도 이 작업의 입력이다(리소스 읽기). 못 읽으면 검사 없이 굽지 않는다.
                DataCatalogReferenceRules references;
                if ( references.loadFromAsset() == false )
                    return CookStep::Failed;
                // 목록도 입력이다 — 파일이 생기거나 사라지면 다시 베이크한다(`CookJob::collectFiles` 가 기록한다).
                vector<string> listSourceFile;
                if ( CookJob::collectFiles( domainDir, "", listSourceFile ) == false )
                    return CookStep::Failed;
                vector<string> listStale;
                for ( const string& absPath : listSourceFile )
                {
                    string relativePath;
                    if ( FileUtil::tryMakePathUnderRoot( domainDir, absPath, relativePath ) == false )
                        continue;
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
                    if ( selection._setIncluded.contains( makeAssetID( domain, relativePath ) ) == false )
                        continue; // 뿌리에서 참조로 닿지 않는다 - 이 빌드의 게임이 쓰지 않는다(`PackReachResult`)
                    if ( addSourceFile( writer, relativePath, absPath, params._resourceRoot, references, ioTiming ) == false )
                        return CookStep::Failed;
                }
                if ( listStale.empty() == false )
                    SW_LOG_WARNING_ID( "PackCooker.PackCookStaleCooked", "Pack cook: %# has %# stale cooked files in the source tree (first: %#) - delete them; they are not packed", domain, listStale.size(),
                                       listStale.front() );

                vector<string> listStagedFile;
                if ( CookJob::collectFiles( stagedDir, "", listStagedFile ) == false )
                    return CookStep::Failed;
                DroppedShaderList droppedRequested; // 닿은 출처가 요청했는데 원본이 닿지 않아 뺀 것 — 런타임이 찾을 셰이더가 팩에 없다(경고)
                DroppedShaderList droppedOther;     // 닿지 않은 출처만 요청했거나 요청 밖(낡은 잔재 등) — 정보 줄
                for ( const string& absPath : listStagedFile )
                {
                    string relativePath;
                    if ( FileUtil::tryMakePathUnderRoot( stagedDir, absPath, relativePath ) == false )
                        continue;
                    if ( PackCooker::shouldIncludeStagedFile( relativePath, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    // 산출물은 그 원본(같은 줄기의 원본 · 셰이더 원본)이 쓰일 때만 담는다 - 쓰지 않는 씬의 쿠킹본 · 쓰지 않는 셰이더의 바이너리를 싣지 않는다.
                    if ( selection._staged.selects( domain, relativePath ) == false )
                    {
                        const string assetID = makeAssetID( domain, relativePath );
                        if ( StringUtil::contains( assetID, "/shaders/bin/" ) )
                            ( PackCooker::isRequestedByReachedOrigin( params._mapRequestedShaderBinary, selection._setIncluded, assetID ) ? droppedRequested : droppedOther ).add( relativePath );
                        continue;
                    }
                    if ( addFile( writer, relativePath, absPath, ioTiming ) == false )
                        return CookStep::Failed;
                }
                if ( droppedRequested._count > 0 )
                    SW_LOG_WARNING_ID( "PackCooker.PackCookDroppedRequestedShader",
                                       "Pack cook: %# drops %# requested %# shader binaries whose source shader is not reached from the cook roots - the runtime will not find them (first: %#)",
                                       domain, droppedRequested._count, params._shaderFolder, droppedRequested._names );
                if ( droppedOther._count > 0 )
                    SW_LOG_INFO_ID( "PackCooker.PackCookDroppedShaderBinaries", "Pack cook: %# drops %# %# shader binaries outside the cook requests (first: %#)", domain, droppedOther._count,
                                    params._shaderFolder, droppedOther._names );

                PackWriteStats writeStats;
                if ( writer.writePackFile( outputPath, &writeStats ) == false )
                    return CookStep::Failed;
                ioTiming._write.add( writeStats );
                SW_LOG_INFO_ID( "PackCooker.PackCookWroteFiles", "Pack cook: wrote %# (%# files, %# bytes before compression)", outputPath, writer.getEntryCount(), writeStats._inputBytes );
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

    bool PackCooker::isRequestedByReachedOrigin( const unordered_map<string, vector<vector<string>>>& mapRequested, const unordered_set<string>& setIncluded, const string& assetID )
    {
        const auto requestIt = mapRequested.find( assetID );
        if ( requestIt == mapRequested.end() )
            return false;
        for ( const vector<string>& listOriginAsset : requestIt->second )
        {
            bool bAllReached = true;
            for ( const string& originAsset : listOriginAsset )
                bAllReached = bAllReached && setIncluded.contains( originAsset );
            if ( bAllReached )
                return true;
        }
        return false;
    }

    string PackCooker::makeRecordID( string_view target, string_view cookedDir, string_view packName )
    {
        // 기록 자리는 프리셋(Debug · Release · Shipping)이 함께 쓰고 팩의 입력(스테이징)은 프리셋마다 다르다 — 스테이징 경로의 해시로 기록을 가른다(헤더 주석).
        const string                            normalizedCookedDir = FileUtil::normalizePath( cookedDir );
        StringBuilder<constant::buffer::kMax32> stagingKey;
        stagingKey.appendFormat( "%016llx", static_cast<unsigned long long>( HashUtil::computeStringHash64( normalizedCookedDir.c_str(), normalizedCookedDir.size(), false ) ) );
        return FileUtil::joinPath( FileUtil::joinPath( FileUtil::joinPath( "packs", target ), stagingKey.view() ), packName );
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

        if ( config._bExcludeRawShaderSource && PackConfig::isRawShaderSource( fileName ) )
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
        outPackCount = 0;
        const bool bMissingParam =
            params._projectRoot.empty() || params._resourceRoot.empty() || params._cookedDir.empty() || params._packOutputDir.empty() || params._shaderFolder.empty();
        if ( bMissingParam )
        {
            SW_LOG_ERROR_ID( "PackCooker.PackCookNeedsContent", "Pack cook needs the project root, the content root, --cooked-dir, --pack-dir and a shader folder" );
            return false;
        }

        vector<string> listDomain;
        if ( PackCookerInternal::collectDomains( params, listDomain ) == false )
            return false;
        if ( listDomain.empty() )
        {
            SW_LOG_ERROR_ID( "PackCooker.PackCookFoundNo", "Pack cook found no domain folder under %#", params._resourceRoot );
            return false;
        }

        CookJobDesc desc;
        desc._cookerName      = "Pack";
        desc._codeFingerprint = CookCodeFingerprint::find( "Pack" );
        desc._target          = params._buildTarget + "-" + params._shaderFolder;
        desc._settingsHash    = PackCookerInternal::computeSettingsHash( config, params );

        PackCookerInternal::PackSelection selection;
        if ( PackCookerInternal::selectFiles( config, params, listDomain, desc, selection ) == false )
            return false;

        uint32                             failedCount = 0;
        PackCookerInternal::PackCookTiming timing;
        const Stopwatch                    totalWatch;
        const int64                        outputHashStart = store.getOutputHashNanos(); // 이 쿠킹의 몫만 센다
        for ( const string& domain : listDomain )
        {
            const string packName   = makePackFileName( domain );
            const string outputPath = FileUtil::joinPath( params._packOutputDir, packName );
            // 기록은 타깃 · 백엔드마다 따로 둔다 — 같은 팩 이름을 다른 설정으로 번갈아 베이크해도 서로의 기록을 덮지 않는다.
            const string outputID    = makeRecordID( desc._target, params._cookedDir, packName );
            CookJobDesc  domainDesc  = desc;
            domainDesc._settingsHash = PackCookerInternal::computeSelectionHash( desc._settingsHash, domain, selection );
            const CookResult result  = store.cookFile( domainDesc, outputID, outputPath, [&]()
             { return PackCookerInternal::cookDomainPack( config, params, domain, outputPath, selection, timing ); } );
            if ( result == CookResult::Failed )
                ++failedCount;
            else if ( result != CookResult::Skipped )
                ++outPackCount;
        }
        PackCookerInternal::logSummary( outPackCount, failedCount, desc._target, totalWatch.getElapsedNanoseconds(),
                                        store.getOutputHashNanos() - outputHashStart, timing );
        return failedCount == 0;
    }
} // namespace sw
