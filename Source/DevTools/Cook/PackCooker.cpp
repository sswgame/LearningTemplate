#include "pch.h"

#include "DevTools/Cook/PackCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Cook/CookStore.h"
#include "DevTools/Cook/PackReachResult.h"
#include "DevTools/Cook/PackSceneChunkPlan.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetPackIndex.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Content/Pack/PackChunkManifest.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackTypes.h"
#include "Engine/Content/Pack/PackWriter.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"
#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "sw/config/CookContract.gen.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct PackCookerInternal
        {
            /**
             * @brief 폴더 @p folder 를 훑은 목록의 파일 @p absPath 를 그 폴더 기준 경로로 바꿉니다. 못 바꾸면 오류를 남기고 false 입니다.
             * @details 목록은 그 폴더 아래에서 나왔으므로 못 바꾸는 것은 폴더 글이 잘못된 때다(`..` 구간 — 포함 판정이 밖으로 본다). 예전에는 조용히 넘겨
             *          `--cooked-dir=../Cooked` 의 스테이징(셰이더 바이너리 · 번역 표)이 팩에서 통째로 빠지고 `-cooked-content` 실행에서야 드러났다.
             */
            [[nodiscard]] static bool makeListedRelativePath( const string& folder, const string& absPath, string& outRelativePath )
            {
                if ( FileUtil::tryMakePathUnderRoot( folder, absPath, outRelativePath ) )
                    return true;
                SW_LOG_ERROR_ID( "PackCooker.ListedFileOutsideFolder", "Pack cook: listed file '%#' is not under its folder '%#' (a '..' segment?) - no pack is written", absPath, folder );
                return false;
            }

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
                    {
                        if ( _count != 0 )
                            _names += ", ";
                        _names += relativePath;
                    }
                    ++_count;
                }
            };

            /** @brief 팩 쿠킹 전체의 단계별 시간입니다(요약 로그). 팩 여럿이 더해 가고, 단계는 한 스레드에서 차례로 돌므로 합이 곧 그 단계에 쓴 시간이다. */
            struct PackCookTiming
            {
                PackWriteStats _write;          ///< 압축 · 배치 · 파일 쓰기(`PackStreamWriter`)
                int64          _readNanos{ 0 }; ///< 원본 · 스테이징 파일 읽기
            };

            /** @brief 팩 항목의 바이트를 어디서 얻는지입니다. */
            enum class PackItemSource : uint8
            {
                Source,    ///< 소스 트리의 파일(데이터 상속을 펼치고 참조를 본다 — `readSourceFile`)
                Staged,    ///< 스테이징 산출물(그대로 — `readStagedFile`)
                Generated, ///< 쿠커가 만든 바이트(`AssetPackIndex`)
            };

            /** @brief 팩에 담을 항목 하나입니다. 모두 모아 경로 해시 순으로 세운 뒤 하나씩 읽어 스트림 쓰기에 넘긴다(팩 전체를 한 번에 들지 않는다). */
            struct PackItem
            {
                uint64         _pathHash{ 0 };
                string         _relativePath;
                string         _absPath;
                PackItemSource _source{ PackItemSource::Source };
                vector<uint8>  _listGeneratedByte; ///< `Generated` 의 바이트
            };

            /** @brief 고르기 결과입니다 — 담을 원본 id, 닿았지만 제외 규칙이 뺀 원본 id(모두 정규화한 리소스 id), 스테이징 산출물의 고르기입니다. */
            struct PackSelection
            {
                unordered_set<string>         _setIncluded;
                unordered_set<string>         _setReferencedExcluded;
                PackStagedSelection           _staged;
                vector<PackSceneReach>        _listSceneReach; ///< 씬 청크로 미룬 씬마다의 닿음(`PackSplitConfig::_listScenePattern` 이 비면 빈 목록)
                PackSceneChunkPlan            _scenePlan;      ///< 기동 필수가 아닌 에셋의 청크
                unordered_map<string, string> _mapStemChunk;   ///< `<폴더 id>/<줄기>` → 청크 — 스테이징 산출물(씬 쿠킹본 · 프리로드 목록)이 원본의 청크를 따른다
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

            /** @brief 원본 파일 하나가 팩에 담길 수 있는지(제외 규칙 · 낡은 쿠킹본이 아닌지)입니다. 고르기의 후보 판정이다. */
            static bool isPackableSource( string_view relativePath, const PackConfig& config, const PackCookParams& params )
            {
                return PackCooker::shouldIncludeFile( relativePath, config, params._shaderFolder, params._buildTarget ) && AssetCookPath::isCookedOutput( relativePath ) == false;
            }

            /** @brief 도메인 기준 상대 경로의 정규화 리소스 id(`game/playground/maps/a.scene.xml`)입니다. */
            static string makeAssetID( string_view domain, string_view relativePath ) { return FileUtil::normalizePath( FileUtil::joinPath( domain, relativePath ) ).str(); }

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
                        (void)FileUtil::tryGetFileSize( FileUtil::toDiskPath( file._absolutePath ), size ); // 크기는 요약에만 쓴다 — 읽지 못하는 파일은 팩 쓰기가 오류로 멈춘다
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
             * @brief 씬 · 프리팹 · 설정의 카탈로그 키 칸(`CatalogKey<Tag>`, 표의 `<Field>`)이 굽는 모든 도메인의 카탈로그 항목을 가리키는지 봅니다. 없는 항목이 하나라도 있으면 false 입니다.
             * @details 팩 작업은 도메인 · 몫마다 따로 돌아 한 팩만 보면 다른 팩(`common`)의 카탈로그를 볼 수 없다 — 모든 도메인을 한 번에 보는 고르기 작업 안에서 한다.
             *          카탈로그는 펼친 글로 본다(물려받은 항목 · 다른 팩 부모의 항목도 항목이다). 표에 칸 줄이 없으면 읽지 않는다.
             */
            [[nodiscard]] static bool validateCatalogKeys( const PackCookParams& params, const vector<PackDomainCandidates>& listCandidates )
            {
                DataCatalogReferenceRules references;
                if ( references.loadFromAsset() == false )
                    return false;
                if ( references.getFieldRows().empty() )
                    return true;
                DataCatalogKeyCheck       check( references );
                DataInheritanceDiskSource source{ params._resourceRoot, engine::getSubsystem<ResourceComposition>().findInheritanceMaxDepth() };
                bool                      bRead = true;
                for ( const PackDomainCandidates& candidates : listCandidates )
                {
                    for ( const PackCandidateFile& file : candidates._listFile )
                    {
                        if ( StringUtil::endsWith( file._assetID, ".xml", true ) == false )
                            continue;
                        string text;
                        if ( AssetIO::readTextFile( file._absolutePath, text ) == false || check.isMentioned( text ) == false )
                            continue;
                        // 쿠킹본을 따로 만드는 종류(씬 · 프리팹)는 펼치지 않는다 — 그 쿠커가 펼치고, 칸 값은 원문에 적힌 그대로다.
                        string flatText;
                        bool   bComposed = false;
                        if ( AssetCookPath::isCookableSource( file._assetID ) == false &&
                             ResourceComposer::compose( text, file._absolutePath, ResourceContext::forPackCook( source ), flatText, bComposed ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackCooker.CatalogKeyFlattenFailed", "Pack cook: could not flatten '%#' to check catalog keys", file._absolutePath );
                            bRead = false;
                            continue;
                        }
                        XMLDocument doc;
                        if ( doc.parse( bComposed ? string_view{ flatText } : string_view{ text }, file._absolutePath ) && doc.getRoot().isValid() )
                            check.addDocument( doc.getRoot(), file._assetID );
                    }
                }
                const bool bValid = check.validate();
                if ( bValid == false )
                    SW_LOG_ERROR_ID( "PackCooker.PackCookBrokenCatalogKey", "Pack cook: catalog key fields name entries that no cooked catalog has (rules in %#) - no pack is written",
                                     DataCatalogReferenceRules::kTableAssetID );
                return bRead && bValid;
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
                AssetReferenceFields properties;
                if ( properties.load( params._projectRoot ) == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.ReferenceFieldsUnreadable", "Pack cook: the asset reference fields under %# could not be read - references cannot be followed", params._projectRoot );
                    return false;
                }

                PackReachFiles                         files;
                PackDiskReachSource                    source;
                vector<PackDomainCandidates>           listCandidates;
                fixed_string<constant::buffer::kMax64> gamePrefix;
                formatstring( gamePrefix.data(), gamePrefix.capacity(), "%#/", constant::path::kGamePack );
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
                        if ( makeListedRelativePath( domainDir, absPath, relativePath ) == false )
                            return false;
                        string assetID = makeAssetID( domain, relativePath );
                        files._setExisting.insert( assetID );
                        candidates._listExisting.push_back( assetID );
                        if ( isPackableSource( relativePath, config, params ) == false )
                            continue;
                        files._setPackable.insert( assetID );
                        source._mapAbsolutePath.try_emplace( assetID, absPath );
                        candidates._listFile.push_back( PackCandidateFile{ std::move( assetID ), absPath } );
                    }
                    if ( StringUtil::startsWith( domain, gamePrefix ) )
                        files._listGameDomain.push_back( FileUtil::normalizePath( domain ).str() );
                }

                AssetCookRoot::appendRoots( files._listCodeRoot );
                files._automation = params._automationContent;
                // 그래프 하나를 전체 고르기 · 기동 필수 · 씬마다의 고르기가 함께 걷는다 — 글마다 한 번 읽고 푼다.
                PackReachGraph  graph( properties, config._cookRules, files, source );
                PackReachResult reach;
                const bool      bReached = PackReachResult::collect( graph, {}, reach );
                logSelection( listCandidates, reach );
                if ( bReached == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackReachFailed", "Pack cook: following references from the cook roots failed (reasons above) - no pack is written" );
                    return false;
                }
                if ( validateCatalogKeys( params, listCandidates ) == false )
                    return false;
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
                return planSceneChunks( config, graph, outSelection );
            }

            /**
             * @brief 씬 청크를 정합니다(`PackSceneChunkPlan`). 씬 패턴이 비면 할 일이 없다.
             * @details 기동 필수는 씬 패턴에 맞는 씬을 뿌리로 쓰지 않고 다시 고른 집합이다 — 게임 설정의 첫 씬처럼 다른 뿌리가 참조로 닿는 씬은 그 안에 든다.
             *          남은 씬마다 그 씬에서만 다시 훑어 닿음을 얻는다.
             */
            [[nodiscard]] static bool planSceneChunks( const PackConfig& config, PackReachGraph& graph, PackSelection& inoutSelection )
            {
                if ( config._split._listScenePattern.empty() )
                    return true;
                unordered_set<string> setDeferredRoot;
                for ( const string& assetID : inoutSelection._setIncluded )
                {
                    if ( matchesAnyPattern( config._split._listScenePattern, assetID ) )
                        setDeferredRoot.insert( assetID );
                }
                PackReachResult startupReach;
                if ( PackReachResult::collect( graph, setDeferredRoot, startupReach ) == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.SceneChunkEssentialFailed", "Pack cook: following references without the deferred scenes failed (reasons above) - no pack is written" );
                    return false;
                }
                vector<string> listScene;
                for ( const string& sceneID : setDeferredRoot )
                {
                    if ( startupReach._setIncluded.contains( sceneID ) == false )
                        listScene.push_back( sceneID );
                }
                VectorUtil::sort( listScene );
                for ( const string& sceneID : listScene )
                {
                    PackReachResult sceneReach;
                    if ( PackReachResult::collectFromRoots( graph, { sceneID }, sceneReach ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.SceneChunkReachFailed", "Pack cook: following references from scene '%#' failed (reasons above) - no pack is written", sceneID );
                        return false;
                    }
                    PackSceneReach& reach = inoutSelection._listSceneReach.emplace_back();
                    reach._sceneID        = sceneID;
                    reach._setReached     = std::move( sceneReach._setIncluded );
                }
                expected<PackSceneChunkPlan> plan = PackSceneChunkPlan::plan( startupReach._setIncluded, inoutSelection._listSceneReach );
                if ( plan.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.SceneChunkPlanInvalid", "Pack cook: scene chunks could not be planned - %# (%#)", plan.error().message(), plan.error().detail() );
                    return false;
                }
                inoutSelection._scenePlan = std::move( *plan );
                for ( const auto& [assetID, chunk] : inoutSelection._scenePlan._mapAssetChunk )
                {
                    inoutSelection._mapStemChunk.try_emplace( makeStemKey( assetID ), chunk );
                }
                SW_LOG_INFO_ID( "PackCooker.SceneChunkPlan", "Pack cook: %# scenes deferred to scene chunks, %# assets outside the startup set (%# startup assets)",
                                static_cast<uint64>( listScene.size() ), static_cast<uint64>( inoutSelection._scenePlan._mapAssetChunk.size() ), static_cast<uint64>( startupReach._setIncluded.size() ) );
                return true;
            }

            /** @brief `<폴더 id>/<첫 . 앞 줄기>` 입니다 — 산출물과 그 원본이 같은 키다(`PackStagedSelection` 과 같은 규칙). */
            static string makeStemKey( string_view assetID )
            {
                const size_t                             slash    = assetID.rfind( '/' );
                const string_view                        fileName = slash == string_view::npos ? assetID : assetID.substr( slash + 1 );
                const string_view                        folder   = slash == string_view::npos ? string_view{} : assetID.substr( 0, slash );
                StringBuilder<constant::buffer::kMax512> builder;
                builder.appendFormat( "%#/%#", folder, fileName.substr( 0, fileName.find( '.' ) ) );
                return string( builder.view() );
            }

            /** @brief 항목 하나의 씬 청크입니다 — 원본은 계획의 것, 스테이징 산출물은 같은 줄기 원본의 것. 없으면 빈 글(기본 청크)입니다. */
            static string findAssignedChunk( const PackSelection& selection, const string& domain, const PackItem& item )
            {
                const string assetID = makeAssetID( domain, item._relativePath );
                if ( item._source == PackItemSource::Source )
                {
                    const auto itChunk = selection._scenePlan._mapAssetChunk.find( assetID );
                    return itChunk != selection._scenePlan._mapAssetChunk.end() ? itChunk->second : string{};
                }
                if ( item._source == PackItemSource::Staged )
                {
                    const auto itChunk = selection._mapStemChunk.find( makeStemKey( assetID ) );
                    return itChunk != selection._mapStemChunk.end() ? itChunk->second : string{};
                }
                return {};
            }

            /** @brief 도메인 팩 작업의 설정 해시입니다 — 쿠킹 설정 해시에 그 도메인에서 고른 파일 목록(정렬)을 섞는다. 다른 팩의 참조가 바뀌어 고른 것이 달라지면 다시 굽는다. */
            static uint64 computeSelectionHash( uint64 settingsHash, const string& domain, const PackSelection& selection )
            {
                StringBuilder<constant::buffer::kMax256> prefix;
                prefix.appendFormat( "%#/", FileUtil::normalizePath( domain ).str() );
                vector<string> listID;
                for ( const unordered_set<string>* pSet : { &selection._setIncluded, &selection._setReferencedExcluded } )
                {
                    for ( const string& assetID : *pSet )
                    {
                        if ( StringUtil::startsWith( assetID, prefix.view() ) )
                            listID.push_back( assetID );
                    }
                }
                VectorUtil::sort( listID );
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
                    if ( FileUtil::isDirectory( FileUtil::toDiskPath( FileUtil::joinPath( params._resourceRoot, pShared ) ) ) )
                        outListDomain.emplace_back( pShared );
                }
                const string gameRoot = FileUtil::joinPath( params._resourceRoot, constant::path::kGamePack );
                if ( params._gamePackFolder.empty() == false )
                {
                    if ( FileUtil::isDirectory( FileUtil::toDiskPath( FileUtil::joinPath( gameRoot, params._gamePackFolder ) ) ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.GamePackFolderDoes", "Game pack folder '%#' does not exist under %#", params._gamePackFolder, gameRoot );
                        return false;
                    }
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "%#/%#", constant::path::kGamePack, params._gamePackFolder );
                    outListDomain.push_back( string( builder.view() ) );
                    return true;
                }
                // 게임 폴더가 없으면 공용 팩만 베이크한다. 도중에 멈춘 목록이면 빠진 게임 팩을 모른 채 넘어가므로 실패다.
                return AssetPaths::collectGamePackDomains( params._resourceRoot, outListDomain );
            }

            /**
             * @brief 굽는 게임 팩마다 그 게임 프리셋(`Config/Game/<게임>.json`)의 쿠킹 규칙(`GameConfig::_cookRules`)을 @p inoutRules 뒤에 합칩니다(`PackCookRules::mergeGameRules`).
             * @details 굽지 않는 게임의 규칙은 합치지 않는다. 게임 규칙의 항상 담기 패턴은 그 게임 팩 루트 아래로 묶여 다른 게임 팩에 새지 않는다. 프리셋을 읽지 못하면 오류입니다.
             *          파일 이름 순으로 합친다(결과가 디렉터리 순서에 기대지 않게). 합친 규칙은 설정 해시에 들어가 게임 규칙이 바뀌면 다시 굽는다.
             */
            [[nodiscard]] static bool mergeGameCookRules( const PackCookParams& params, const vector<string>& listDomain, PackCookRules& inoutRules )
            {
                const string   folder = FileUtil::joinPath( params._projectRoot, GameConfig::kConfigFolder );
                vector<string> listConfigPath;
                const bool     bListed = FileUtil::forEachDirectoryEntry( FileUtil::toDiskPath( folder ), false, [&listConfigPath]( const DirectoryEntry& entry )
                    {
                    if ( entry._bDirectory == false && StringUtil::endsWith( entry._path, ".json" ) )
                        listConfigPath.emplace_back( entry._path );
                    return true;
                } );
                if ( bListed == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.GameConfigFolderUnreadable", "Pack cook: the game preset folder %# could not be listed - game cook rules cannot be merged", folder );
                    return false;
                }
                VectorUtil::sort( listConfigPath );
                for ( const string& configPath : listConfigPath )
                {
                    GameConfig gameConfig;
                    if ( ConfigManager::readConfigFile( gameConfig, configPath ) != ConfigReadResult::Loaded )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.GameConfigUnreadable", "Pack cook: the game preset %# could not be read (reasons above)", configPath );
                        return false;
                    }
                    const PathKey packRoot = FileUtil::normalizePath( gameConfig._packRoot.get() );
                    for ( const string& domain : listDomain )
                    {
                        if ( FileUtil::normalizePath( domain ) == packRoot )
                            inoutRules.mergeGameRules( packRoot.str(), gameConfig._cookRules );
                    }
                }
                return true;
            }

            /** @brief 작업 키의 설정 해시입니다 — 담는 규칙과 팩 바이트를 정하는 값 모두(설정 전체 · 셰이더 폴더 · 빌드 타깃). 설정을 글로 못 쓰면 오류입니다. */
            [[nodiscard]] static expected<uint64> computeSettingsHash( const PackConfig& config, const PackCookParams& params )
            {
                expected<string> configText = JSONSerializer::serialize( &config, *PackConfig::StaticType() );
                if ( configText.has_value() == false )
                    return unexpected( configText.error() );
                string canonical = std::move( *configText );
                canonical.append( "\nshader=" ).append( params._shaderFolder ).append( "\ntarget=" ).append( params._buildTarget );
                // 팩 형식 버전도 설정이다 - 형식을 올리면 옛 형식의 산출물을 재사용하지 않는다(리더가 거절할 팩을 남기지 않게).
                utf8 arrFormat[constant::buffer::kMax32];
                formatstring( arrFormat, constant::buffer::kMax32, "\nformat=%#", kPackFormatVersion );
                canonical.append( arrFormat );
                return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
            }

            /**
             * @brief 파일 하나를 읽어 팩에 담습니다. 디스크 읽기 입구(`AssetIO::readBinaryFile`)가 읽기와 작업 입력 기록(`CookJob::allowDiskRead`)을 함께 한다.
             * @details 팩은 마운트된 팩이 아니라 느슨한 트리 · 스테이징(리소스 루트 밖)에서 베이크하므로 디스크 경로로 읽는다. 예전에는 `FileUtil` 로 읽고 입력을 손으로 기록했다(같은 일 둘).
             *          읽기 시간에는 입력 해시가 들어 있다.
             */
            [[nodiscard]] static bool readStagedFile( const string& absPath, PackCookTiming& ioTiming, vector<uint8>& outBytes )
            {
                vector<uint8>&  bytes = outBytes;
                const Stopwatch readWatch;
                const bool      bRead = AssetIO::readBinaryFile( absPath, bytes );
                ioTiming._readNanos += readWatch.getElapsedNanoseconds();
                if ( bRead == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackCookCouldNot", "Pack cook: could not read '%#'", absPath );
                    return false;
                }
                return true;
            }

            /**
             * @brief 소스 파일 하나를 팩에 담습니다. 부모를 가리키는 데이터(`_base`)는 펼친 한 벌을 담습니다 — 런타임은 펼친 결과만 읽어 비용이 없다(`DataInheritance`).
             * @details 부모도 디스크 입구로 읽으므로 이 작업의 입력이 되어, 부모가 바뀌면 팩을 다시 굽는다. 스스로 쿠킹본을 만드는 종류(프리팹 변형 — `PrefabCooker::cookAll`)는 그 쿠커가 펼쳤다.
             *          펼치지 못하면 쿠킹 실패다 — 부모 없이 자식 칸만 담지 않는다. 카탈로그 참조 규칙(@p references)이 있는 루트는 펼친 글로 참조를 보고, 필수 참조가 깨졌으면
             *          쿠킹 실패다 — 런타임 카탈로그 읽기와 같은 표 · 같은 검사라 실행해야 드러나던 깨진 id 가 팩에 담기지 않는다.
             */
            [[nodiscard]] static bool readSourceFile( string_view relativePath, const string& absPath, string_view contentRoot, const DataCatalogReferenceRules& references,
                                                      PackCookTiming& ioTiming, vector<uint8>& outBytes )
            {
                vector<uint8>&  bytes = outBytes;
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
                    return true;
                DataInheritanceDiskSource source{ contentRoot, engine::getSubsystem<ResourceComposition>().findInheritanceMaxDepth() };
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
                    bytes.assign( flatText.begin(), flatText.end() );
                return true;
            }

            /**
             * @brief 모은 항목을 경로 해시 순으로 세워 하나씩 읽고 @p outputPath 에 스트림으로 씁니다. 항목은 하나씩(창 크기까지)만 들고 내용 해시를 함께 센다.
             * @return 같은 경로가 두 번이거나 읽기 · 쓰기가 실패하면 이유를 오류로 남기고 false 입니다.
             */
            [[nodiscard]] static bool writePackItems( const PackConfig& config, const PackWriterDesc& writerDesc, const string& outputPath, const PackCookParams& params, const DataCatalogReferenceRules& references,
                                                      vector<PackItem>& inoutListItem, PackCookTiming& ioTiming, PackWriteResult& outResult )
            {
                VectorUtil::sort( inoutListItem, []( const PackItem& left, const PackItem& right )
                { return left._pathHash < right._pathHash; } );
                PackStreamWriter writer;
                expected<void>   step = writer.open( outputPath, writerDesc );
                for ( size_t index = 0; index < inoutListItem.size() && step.has_value(); ++index )
                {
                    PackItem&     item = inoutListItem[index];
                    vector<uint8> bytes;
                    bool          bRead = true;
                    if ( item._source == PackItemSource::Source )
                        bRead = readSourceFile( item._relativePath, item._absPath, params._resourceRoot, references, ioTiming, bytes );
                    else if ( item._source == PackItemSource::Staged )
                        bRead = readStagedFile( item._absPath, ioTiming, bytes );
                    else
                        bytes = std::move( item._listGeneratedByte );
                    if ( bRead == false )
                        return false; // 읽기가 이유를 남겼다. 소멸하는 쓰기가 임시 파일을 지운다
                    const expected<PackEntryStorage> storage = config._compression.selectEntryStorage( item._relativePath );
                    if ( storage.has_value() == false )
                        return false; // 규칙이 이유를 남겼다. 소멸하는 쓰기가 임시 파일을 지운다
                    step = writer.append( item._relativePath, *storage, std::move( bytes ) );
                }
                expected<PackWriteResult> closed = step.has_value() ? writer.close() : expected<PackWriteResult>( unexpected( step.error() ) );
                if ( closed.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackWriteFailed", "Pack cook: could not write %# - %# '%#'", outputPath, closed.error().message(), closed.error().detail() );
                    return false;
                }
                outResult = std::move( *closed );
                return true;
            }

            /**
             * @brief 소스 파일 하나의 정체성(GUID)입니다 — 우리 형식 글은 머리, 원본은 곁의 `.meta`, 임포트 산출물은 원본의 `.meta` 와 산출물 이름. 없으면 없음입니다.
             * @details 디스크 입구(`AssetIO::readTextFile`)로 읽는다 — 이 작업의 입력이 되어 GUID 가 바뀌면 팩을 다시 굽는다. 규칙은 저작 색인(`AssetDatabase::build`)과 같다.
             */
            [[nodiscard]] static optional<AssetGUID> readPackedIdentity( const AssetIdentityRules& rules, AssetIdentityKind kind, const string& assetID, const string& absPath )
            {
                if ( kind != AssetIdentityKind::Header && kind != AssetIdentityKind::Source )
                    return nullopt;
                const string metaAbs = AssetMeta::makePath( absPath );
                string       text;
                AssetMeta    meta;
                if ( kind == AssetIdentityKind::Header && AssetIO::readTextFile( absPath, text ) )
                {
                    const optional<AssetGUID> headerGUID = AssetGUIDHeader::read( text )._guid;
                    if ( headerGUID.has_value() )
                        return headerGUID;
                }
                if ( kind == AssetIdentityKind::Source && FileUtil::isRegularFile( FileUtil::toDiskPath( metaAbs ) ) && AssetIO::readTextFile( metaAbs, text ) )
                    return meta.parseXML( text, AssetMeta::makePath( assetID ) );
                if ( rules.isUnderDerivedFolder( assetID ) == false )
                    return nullopt;
                const string_view           contentRoot = string_view( absPath ).substr( 0, absPath.size() - assetID.size() ); // 원본 경로는 리소스 id 로 끝난다(`makeAssetID`)
                vector<AssetImportedSource> listSource;
                rules.collectImportedSources( assetID, listSource );
                for ( const AssetImportedSource& source : listSource )
                {
                    const string sourceMetaAbs = AssetMeta::makePath( FileUtil::joinPath( contentRoot, source._sourceID ) );
                    if ( FileUtil::isRegularFile( FileUtil::toDiskPath( sourceMetaAbs ) ) == false )
                        continue;
                    const optional<AssetGUID> sourceGUID = AssetIO::readTextFile( sourceMetaAbs, text ) ? meta.parseXML( text, AssetMeta::makePath( source._sourceID ) ) : optional<AssetGUID>{};
                    return sourceGUID.has_value() ? optional<AssetGUID>{ AssetGUID::fromName( *sourceGUID, source._outputName ) } : nullopt;
                }
                return nullopt;
            }

            /**
             * @brief 팩에 담은 소스 파일 하나의 정체성(GUID)을 팩 색인(@p inoutIndex)에 한 줄 더합니다 — 배포본에는 `.meta` 가 없어(PackConfig 가 뺀다) 정체성은 이 색인이 나른다.
             * @details 원본이 없는 산출물(내용 주소 공유 결과 `_shared/`)은 줄이 없다 — 산출물끼리 경로로 가리킨다.
             *          그 밖에 정체성이 없는 파일은 쿠킹 실패다 — 경로로 대신하지 않는다(`py -3 -m Scripts asset-ids --fix`).
             */
            [[nodiscard]] static bool addPackedIdentity( const AssetIdentityRules& rules, const string& domain, const PackItem& item, AssetPackIndex& inoutIndex )
            {
                const string            assetID = makeAssetID( domain, item._relativePath );
                const AssetIdentityKind kind    = rules.classify( assetID );
                if ( kind == AssetIdentityKind::Skipped || kind == AssetIdentityKind::Sidecar )
                    return true;
                const optional<AssetGUID> guid = readPackedIdentity( rules, kind, assetID, item._absPath );
                if ( guid.has_value() == false && rules.isUnderDerivedFolder( assetID ) )
                    return true;
                if ( guid.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackCookNoIdentity", "Pack cook: '%#' has no asset GUID - run py -3 -m Scripts asset-ids --fix", assetID );
                    return false;
                }
                inoutIndex._listEntry.push_back( AssetPackIndexEntry{ *guid, FileUtil::normalizeSeparators( item._relativePath ) } );
                return true;
            }

            /** @brief 도메인 리다이렉트 표의 정체성 대체 줄을 팩 색인에 더합니다. 읽지 못했거나 틀렸으면 이유가 남고 false 입니다. */
            [[nodiscard]] static bool addIdentityRedirects( const PackItem& item, AssetPackIndex& inoutIndex )
            {
                string text;
                if ( AssetIO::readTextFile( item._absPath, text ) == false )
                    return false;
                const expected<vector<AssetIdentityRedirect>> rows = AssetRedirect::parseIdentityRows( text, item._absPath );
                if ( rows.has_value() == false )
                    return false;
                inoutIndex._listRedirect.insert( inoutIndex._listRedirect.end(), rows->begin(), rows->end() );
                return true;
            }

            /**
             * @brief 팩 쿠킹 요약 한 줄을 남깁니다 — 단계별 ms 로 병렬 압축의 효과를 잰다.
             * @details `compress` 는 `벽시계 / CPU 합` 이다(CPU 합이 직렬로 돌았다면 든 시간이라 둘의 비가 병렬 효과). `hash` 는 입력 내용 해시(`readSourceFile` · `readStagedFile`)와 출력 내용 해시(`CookStore` 의 재사용 판정 · 기록)에 쓴 시간이고,
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

            /** @brief 도메인 하나에서 담을 항목의 목록과 알릴 것(낡은 잔재 · 뺀 셰이더 바이너리)입니다. 파일은 읽지 않는다(목록만). */
            struct DomainListing
            {
                vector<PackItem>  _listItem;
                vector<PackItem>  _listReferencedExcluded; ///< 닿았지만 싣지 않는 원본 — 정체성 색인에만 줄이 든다
                vector<string>    _listStale;
                DroppedShaderList _droppedRequested; ///< 닿은 출처가 요청했는데 원본이 닿지 않아 뺀 것 — 런타임이 찾을 셰이더가 팩에 없다(경고)
                DroppedShaderList _droppedOther;     ///< 닿지 않은 출처만 요청했거나 요청 밖(낡은 잔재 등) — 정보 줄
            };

            /**
             * @brief 도메인의 원본 · 스테이징 목록에서 담을 항목을 고릅니다(정체성 색인 항목은 넣지 않는다 — 첫 팩의 작업이 만든다). 나누기와 팩 작업이 같은 함수를 써 같은 목록을 본다.
             * @details 목록은 `CookJob::collectFiles` 로 읽어 작업 안이면 그 작업의 입력이 된다(파일이 생기거나 사라지면 다시 굽는다).
             */
            [[nodiscard]] static bool collectDomainItems( const PackConfig& config, const PackCookParams& params, const string& domain, const PackSelection& selection, DomainListing& outListing )
            {
                const string domainDir = FileUtil::joinPath( params._resourceRoot, domain );
                const string stagedDir = FileUtil::joinPath( params._cookedDir, domain );
                const auto   addItem   = [&outListing]( const string& relativePath, const string& absPath, PackItemSource source )
                { outListing._listItem.push_back( PackItem{ PackStreamWriter::makePathHash( relativePath ), relativePath, absPath, source, {} } ); };

                vector<string> listSourceFile;
                if ( CookJob::collectFiles( domainDir, "", listSourceFile ) == false )
                    return false;
                for ( const string& absPath : listSourceFile )
                {
                    string relativePath;
                    if ( makeListedRelativePath( domainDir, absPath, relativePath ) == false )
                        return false;
                    if ( selection._setReferencedExcluded.contains( makeAssetID( domain, relativePath ) ) )
                        outListing._listReferencedExcluded.push_back( PackItem{ PackStreamWriter::makePathHash( relativePath ), relativePath, absPath, PackItemSource::Source, {} } );
                    if ( PackCooker::shouldIncludeFile( relativePath, config, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    // 산출물은 스테이징에만 있다. 소스 트리에 남은 것은 낡은 쿠킹 잔재이고, Dev 런타임이 소스가 없을 때 그것으로 물러나 실패를 가리므로 넣지 않는다.
                    if ( AssetCookPath::isCookedOutput( relativePath ) )
                    {
                        outListing._listStale.push_back( relativePath );
                        continue;
                    }
                    if ( selection._setIncluded.contains( makeAssetID( domain, relativePath ) ) == false )
                        continue; // 뿌리에서 참조로 닿지 않는다 - 이 빌드의 게임이 쓰지 않는다(`PackReachResult`)
                    addItem( relativePath, absPath, PackItemSource::Source );
                }

                vector<string> listStagedFile;
                if ( CookJob::collectFiles( stagedDir, "", listStagedFile ) == false )
                    return false;
                for ( const string& absPath : listStagedFile )
                {
                    string relativePath;
                    if ( makeListedRelativePath( stagedDir, absPath, relativePath ) == false )
                        return false;
                    if ( PackCooker::shouldIncludeStagedFile( relativePath, params._shaderFolder, params._buildTarget ) == false )
                        continue;
                    // 산출물은 그 원본(같은 줄기의 원본 · 셰이더 원본)이 쓰일 때만 담는다 - 쓰지 않는 씬의 쿠킹본 · 쓰지 않는 셰이더의 바이너리를 싣지 않는다.
                    if ( selection._staged.isSelected( domain, relativePath ) == false )
                    {
                        const string assetID = makeAssetID( domain, relativePath );
                        if ( StringUtil::contains( assetID, "/shaders/bin/" ) )
                            ( PackCooker::isRequestedByReachedOrigin( params._mapRequestedShaderBinary, selection._setIncluded, assetID ) ? outListing._droppedRequested : outListing._droppedOther )
                                .add( relativePath );
                        continue;
                    }
                    addItem( relativePath, absPath, PackItemSource::Staged );
                }
                return true;
            }

            /** @brief 목록을 고르며 본 것(낡은 잔재 · 뺀 셰이더 바이너리)을 한 번 알립니다 — 나누기 단계가 부른다(팩 몫마다 되풀이하지 않는다). */
            static void logListingNotes( const PackCookParams& params, const string& domain, const DomainListing& listing )
            {
                if ( listing._listStale.empty() == false )
                    SW_LOG_WARNING_ID( "PackCooker.PackCookStaleCooked", "Pack cook: %# has %# stale cooked files in the source tree (first: %#) - delete them; they are not packed", domain,
                                       listing._listStale.size(), listing._listStale.front() );
                if ( listing._droppedRequested._count > 0 )
                    SW_LOG_WARNING_ID( "PackCooker.PackCookDroppedRequestedShader",
                                       "Pack cook: %# drops %# requested %# shader binaries whose source shader is not reached from the cook roots - the runtime will not find them (first: %#)",
                                       domain, listing._droppedRequested._count, params._shaderFolder, listing._droppedRequested._names );
                if ( listing._droppedOther._count > 0 )
                    SW_LOG_INFO_ID( "PackCooker.PackCookDroppedShaderBinaries", "Pack cook: %# drops %# %# shader binaries outside the cook requests (first: %#)", domain,
                                    listing._droppedOther._count, params._shaderFolder, listing._droppedOther._names );
            }

            /** @brief 팩 몫 작업의 설정 해시입니다 — 도메인의 고르기 해시에 청크 · 번호 · 담을 경로 해시를 섞는다(경계가 옮겨지면 다시 굽는다). */
            static uint64 computePartHash( uint64 selectionHash, const PackPart& part )
            {
                uint64 hash = HashUtil::computeStringHash64( part._chunk.c_str(), part._chunk.size(), false, selectionHash );
                hash        = HashUtil::appendValue64( hash, part._number );
                for ( const uint64 pathHash : part._listPathHash )
                {
                    hash = HashUtil::appendValue64( hash, pathHash );
                }
                return hash;
            }

            /** @brief 팩 폴더의 쿠커 장부 파일 이름입니다 — 쿠커가 쓴 팩 파일 이름을 한 줄씩 적는다(팩이 아니라 싣지 않는다). 형제 팩 정리는 이 장부에 있는 것만 지운다. */
            static constexpr string_view kCookedPackLedgerName = "CookedPacks.txt";

            /** @brief 팩 폴더의 장부를 읽습니다. 없으면 빈 장부입니다. 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool readCookedPackLedger( const string& packOutputDir, unordered_set<string>& outSetName )
            {
                const string ledgerPath = FileUtil::joinPath( packOutputDir, kCookedPackLedgerName );
                if ( FileUtil::isRegularFile( FileUtil::toDiskPath( ledgerPath ) ) == false )
                    return true;
                string text;
                if ( FileUtil::readTextFile( FileUtil::toDiskPath( ledgerPath ), text ) == false )
                    return false;
                size_t lineStart = 0;
                while ( lineStart < text.size() )
                {
                    size_t lineEnd         = text.find( '\n', lineStart );
                    lineEnd                = lineEnd == string::npos ? text.size() : lineEnd;
                    const string_view name = StringUtil::trim( string_view( text ).substr( lineStart, lineEnd - lineStart ) );
                    if ( name.empty() == false )
                        outSetName.insert( string( name ) );
                    lineStart = lineEnd + 1;
                }
                return true;
            }

            /**
             * @brief 미루는 청크의 목록을 굽은 도메인마다 팩 폴더에 씁니다(`PackCooker::writeChunkManifests`). 쓰기 전에 씬마다 닿는 에셋이 기동 필수거나 그 씬의 청크에 놓였는지 본다 —
             *        빠지면 그 씬을 열 때 에셋을 찾지 못하므로 쿠킹 오류다(`PackSceneChunkPlan::findUncovered`).
             * @param mapPlacedChunk 담긴 에셋 id → 놓인 청크(기동 필수는 빈 글, 미룬 청크는 `<도메인 줄기>.<청크>`)
             */
            [[nodiscard]] static bool writeChunkManifest( const string& packOutputDir, const vector<string>& listDomain, const PackSelection& selection,
                                                          const unordered_map<string, string>& mapPlacedChunk, unordered_map<string, vector<string>>& mapChunkPack )
            {
                const unordered_set<string> setStartupChunk{ string{} };
                PackChunkManifest           manifest;
                manifest._listScene                    = PackSceneChunkPlan::makeSceneChunks( selection._listSceneReach, mapPlacedChunk, setStartupChunk );
                const vector<PackChunkIssue> listIssue = PackSceneChunkPlan::findUncovered( selection._listSceneReach, mapPlacedChunk, setStartupChunk, manifest._listScene );
                for ( const PackChunkIssue& gap : listIssue )
                {
                    SW_LOG_ERROR_ID( "PackCooker.SceneChunkGap", "Pack cook: scene '%#' uses '%#' but it is in chunk '%#' - neither a startup chunk nor one the scene mounts", gap._sceneID, gap._assetID,
                                     gap._chunk );
                }
                if ( listIssue.empty() == false )
                    return false;
                for ( auto& [chunk, listPack] : mapChunkPack )
                {
                    VectorUtil::sort( listPack );
                    manifest._listChunk.push_back( PackChunkFiles{ chunk, listPack } );
                }
                VectorUtil::sort( manifest._listChunk, []( const PackChunkFiles& left, const PackChunkFiles& right )
                { return left._name < right._name; } );
                return PackCooker::writeChunkManifests( packOutputDir, listDomain, manifest );
            }

            /** @brief 장부를 이름 순으로 씁니다. */
            [[nodiscard]] static bool writeCookedPackLedger( const string& packOutputDir, const unordered_set<string>& setName )
            {
                vector<string> listName( setName.begin(), setName.end() );
                VectorUtil::sort( listName );
                string text;
                for ( const string& name : listName )
                {
                    text.append( name ).append( "\n" );
                }
                return FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( packOutputDir ) ) && FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( packOutputDir, kCookedPackLedgerName ) ), text );
            }

            /**
             * @brief 팩 폴더에서 도메인의 형제 팩(`<도메인>.*.pack`) 가운데 이번 쿠킹이 쓰지 않은 것을 정리합니다. 기본 이름(`<도메인>.pack`)은 늘 쓰므로 건드리지 않는다.
             * @details 지우는 것은 쿠커 장부(@p inoutSetLedger — 지난 쿠킹들이 쓴 팩)에 있는 것뿐이고, 장부에서도 뺀다. 장부에 없는 형제는 사람이 둔 파일일 수 있어 경고만 남긴다
             *          (그대로 두면 마운트가 같은 도메인의 겹침으로 거절할 수 있다).
             * @return 목록을 못 읽거나 지우지 못하면 이유를 남기고 false 입니다.
             */
            [[nodiscard]] static bool removeStaleSiblingPacks( const string& packOutputDir, const string& domain, const vector<string>& listWrittenName, unordered_set<string>& inoutSetLedger )
            {
                if ( FileUtil::isDirectory( FileUtil::toDiskPath( packOutputDir ) ) == false )
                    return true;
                vector<string> listPackFile;
                if ( FileUtil::collectFiles( FileUtil::toDiskPath( packOutputDir ), ".pack", listPackFile, false ) == false )
                    return false;
                const string                             baseName = PackCooker::makePackFileName( domain );
                StringBuilder<constant::buffer::kMax256> prefixBuilder;
                prefixBuilder.appendFormat( "%#.", string_view( baseName ).substr( 0, baseName.size() - string_view( ".pack" ).size() ) );
                const string prefix( prefixBuilder.view() );
                bool         bAllGone = true;
                for ( const string& packPath : listPackFile )
                {
                    string_view fileName;
                    FileUtil::getFileNamePart( packPath, fileName );
                    const string name     = string( fileName );
                    const bool   bSibling = StringUtil::startsWith( fileName, prefix ) && name != baseName;
                    if ( bSibling == false || VectorUtil::contains( listWrittenName, name ) )
                        continue;
                    if ( inoutSetLedger.contains( name ) == false )
                    {
                        SW_LOG_WARNING_ID( "PackCooker.PackCookUnknownSibling", "Pack cook: %# looks like a split pack of %# but the cooker did not write it (not in %#) - it is left; remove it if it is stale",
                                           packPath, domain, kCookedPackLedgerName );
                        continue;
                    }
                    if ( FileUtil::removeFile( FileUtil::toDiskPath( packPath ) ) )
                    {
                        inoutSetLedger.erase( name );
                        SW_LOG_INFO_ID( "PackCooker.PackCookRemovedSibling", "Pack cook: removed %# - %# is split into fewer packs now", packPath, domain );
                    }
                    else
                        bAllGone = false;
                }
                return bAllGone;
            }

            /**
             * @brief 도메인의 팩 몫 하나(@p part)를 @p outputPath 에 씁니다. 작업 범위 안에서 부른다. 단계별 시간은 @p ioTiming 에 더한다.
             * @details 목록(@p listing)은 나누기가 한 번 고른 것을 받아 이 몫의 경로 해시만 담는다 — 목록이 바뀌면 몫의 경로 해시가 설정 해시에 들어 있어 다시 굽는다.
             *          첫 팩(기본 청크 0 번)은 도메인 전체의 정체성 색인을 함께 담는다.
             */
            static CookStreamedStep cookDomainPack( const PackConfig& config, const PackCookParams& params, const string& domain, const string& outputPath, const DomainListing& listing,
                                                    const PackPart& part, PackCookTiming& ioTiming )
            {
                constexpr CookStreamedStep kFailed{ CookStep::Failed, 0 };

                PackWriterDesc writerDesc;
                writerDesc._compression      = config._compression._codec;
                writerDesc._compressionLevel = config._compression._level;
                writerDesc._layoutMode       = config._layout._mode;
                writerDesc._maxHolePercent   = static_cast<uint32>( config._layout._maxHolePercent );
                writerDesc._previousPackPath = outputPath; // 덮어쓸 팩이 앞 빌드다(`Slot` 이 안 바뀐 항목의 자리를 지킨다). 없으면 첫 배치다
                writerDesc._mountDomain      = domain;     // 형제 팩도 같은 도메인을 적는다 — 리더는 그대로 붙이고 마운트가 겹침을 거절한다
                writerDesc._blockSize        = config._compression._blockSize;
                writerDesc._writeWindowBytes = config._compression._writeWindowBytes;

                // 카탈로그 참조 규칙 — 표도 이 작업의 입력이다(리소스 읽기). 못 읽으면 검사 없이 굽지 않는다.
                DataCatalogReferenceRules references;
                if ( references.loadFromAsset() == false )
                    return kFailed;
                vector<PackItem> listItem;
                for ( const PackItem& item : listing._listItem )
                {
                    if ( part.contains( item._pathHash ) )
                        listItem.push_back( item );
                }
                if ( part.isFirstPack() )
                {
                    // 정체성 규칙 — 도메인의 GUID → 팩 안 경로 색인을 첫 팩에 담는다(`AssetPackIndex`). 못 읽으면 색인 없이 굽지 않는다.
                    AssetIdentityRules identityRules;
                    AssetPackIndex     identityIndex;
                    if ( identityRules.load( ProcessRoots::get()._project.str() ) == false )
                        return kFailed;
                    for ( const PackItem& item : listing._listItem ) // 색인은 도메인 전체다 — 다른 몫에 담긴 원본도 센다
                    {
                        if ( item._source == PackItemSource::Source && addPackedIdentity( identityRules, domain, item, identityIndex ) == false )
                            return kFailed;
                        // 정체성 대체 줄은 색인이 나른다 — 런타임은 색인만 합친다.
                        if ( item._source == PackItemSource::Source && item._relativePath == AssetRedirect::kFileName && addIdentityRedirects( item, identityIndex ) == false )
                            return kFailed;
                    }
                    // 닿았지만 이 대상이 싣지 않는 원본(셰이더 원본, 전용 서버의 텍스처)도 줄은 싣는다 — 줄이 없으면 그것을 가리키는 GUID 참조가 풀리지 않아 읽기가 진다.
                    for ( const PackItem& item : listing._listReferencedExcluded )
                    {
                        const string              assetID = makeAssetID( domain, item._relativePath );
                        const optional<AssetGUID> guid    = readPackedIdentity( identityRules, identityRules.classify( assetID ), assetID, item._absPath );
                        if ( guid.has_value() )
                            identityIndex._listEntry.push_back( AssetPackIndexEntry{ *guid, FileUtil::normalizeSeparators( item._relativePath ) } );
                    }
                    const expected<string> indexText = identityIndex.makeText();
                    if ( indexText.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "PackCooker.PackCookIndexInvalid", "Pack cook: %# asset index - %# '%#'", domain, indexText.error().message(), indexText.error().detail() );
                        return kFailed;
                    }
                    PackItem indexItem{ PackStreamWriter::makePathHash( AssetPackIndex::kFileName ), string( AssetPackIndex::kFileName ), {}, PackItemSource::Generated, {} };
                    indexItem._listGeneratedByte.assign( indexText->begin(), indexText->end() );
                    listItem.push_back( std::move( indexItem ) );
                }

                PackWriteResult writeResult;
                if ( writePackItems( config, writerDesc, outputPath, params, references, listItem, ioTiming, writeResult ) == false )
                    return kFailed;
                // 나누기의 어림은 압축 전 · 펼치기 전 크기다 — 데이터 상속을 펼친 글이 커져 한도를 넘었으면 오류로 멈춘다(한도는 FAT32 같은 바깥 상한이라 넘긴 팩은 못 싣는다).
                if ( writeResult._fileSize > config._split._maxPackBytes )
                {
                    SW_LOG_ERROR_ID( "PackCooker.PackOverSplitLimit", "Pack cook: %# is %# bytes - over the %#-byte split limit (expanded text grew past the estimate); lower _split._maxPackBytes or add a chunk rule",
                                     outputPath, writeResult._fileSize, config._split._maxPackBytes );
                    return kFailed;
                }
                ioTiming._write.add( writeResult._stats );
                SW_LOG_INFO_ID( "PackCooker.PackCookWroteFiles", "Pack cook: wrote %# (%# files, %# bytes before compression)", outputPath, writeResult._stats._entryCount,
                                writeResult._stats._inputBytes );
                return CookStreamedStep{ CookStep::Written, writeResult._contentHash };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PackCooker::makePackFileName( string_view domain, string_view chunk, uint32 number )
    {
        string name = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( domain ).str() );
        std::replace( name.begin(), name.end(), '/', '_' );
        // 기본 청크 0 번은 예전 이름 그대로다 — 배포 스크립트(`Scripts/package/pack.py`) · 도구가 아는 이름이고 나누지 않는 도메인이 대부분이다.
        if ( chunk.empty() == false )
            name.append( "." ).append( chunk );
        if ( chunk.empty() == false || number > 0 )
        {
            StringBuilder<constant::buffer::kMax32> numberText;
            numberText.appendFormat( ".%#", number );
            name.append( numberText.view() );
        }
        name += ".pack";
        return name;
    }

    uint64 PackCooker::estimateEntryPackBytes( string_view relativePath, uint64 sizeBytes )
    {
        const uint64 sector     = kPackSectorAlignment;
        const uint64 blockCount = ( sizeBytes + kPackMinBlockSize - 1 ) / kPackMinBlockSize;
        return ( sizeBytes + sector - 1 ) / sector * sector + sizeof( PackFileEntryOnDisk ) + blockCount * sizeof( PackBlockEntryOnDisk ) + relativePath.size() + 1;
    }

    uint64 PackCooker::estimateFixedPackBytes()
    {
        return sizeof( PackHeader ) + kPackSectorAlignment;
    }

    expected<vector<PackPart>> PackCooker::splitIntoParts( const vector<PackSplitEntry>& listEntry, const PackSplitConfig& split, const unordered_map<string, string>& mapAssignedChunk )
    {
        // 청크 이름 표 — 기본(빈 글), 이름 붙은 패턴 청크(규칙 순서), 씬 청크(이름순).
        vector<string> listChunkName{ string{} };
        for ( const PackChunkRule& rule : split._listChunkRule )
        {
            listChunkName.push_back( rule._name );
        }
        {
            vector<string> listAssigned;
            for ( const auto& [relativePath, chunk] : mapAssignedChunk )
            {
                VectorUtil::addUnique( listAssigned, chunk );
            }
            VectorUtil::sort( listAssigned );
            for ( const string& chunk : listAssigned )
            {
                for ( const PackChunkRule& rule : split._listChunkRule )
                {
                    if ( StringUtil::equals( rule._name, chunk, true ) )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a scene chunk has the name of a pack chunk rule", chunk } );
                }
                listChunkName.push_back( chunk );
            }
        }
        // 청크 이름은 파일 이름의 한 마디다 — 비었거나 다른 글자가 들었거나 겹치면 이름이 어긋난다.
        for ( size_t ruleIndex = 0; ruleIndex < split._listChunkRule.size(); ++ruleIndex )
        {
            const string& name       = split._listChunkRule[ruleIndex]._name;
            bool          bNameValid = name.empty() == false;
            for ( const utf8 letter : name )
            {
                const bool bLetterOrDigit = ( 'a' <= letter && letter <= 'z' ) || ( 'A' <= letter && letter <= 'Z' ) || ( '0' <= letter && letter <= '9' );
                bNameValid                = bNameValid && ( bLetterOrDigit || letter == '_' || letter == '-' );
            }
            for ( size_t otherIndex = 0; otherIndex < ruleIndex; ++otherIndex )
            {
                bNameValid = bNameValid && StringUtil::equals( split._listChunkRule[otherIndex]._name, name, true ) == false;
            }
            if ( bNameValid == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "pack chunk name must be unique and use letters, digits, '_' and '-'", name } );
        }

        // 청크마다(기본이 먼저, 그다음 규칙 순서) 항목을 경로 해시 순으로 놓는다.
        struct Placed
        {
            uint64 _pathHash{ 0 };
            uint64 _sizeBytes{ 0 };
        };
        vector<vector<Placed>> listChunkPlaced( listChunkName.size() );
        for ( const PackSplitEntry& entry : listEntry )
        {
            size_t chunkIndex = 0;
            for ( size_t ruleIndex = 0; ruleIndex < split._listChunkRule.size() && chunkIndex == 0; ++ruleIndex )
            {
                for ( const string& pattern : split._listChunkRule[ruleIndex]._listPattern )
                {
                    if ( StringUtil::matchesPathPattern( pattern, entry._relativePath ) )
                    {
                        chunkIndex = ruleIndex + 1;
                        break;
                    }
                }
            }
            if ( chunkIndex == 0 )
            {
                if ( const auto itAssigned = mapAssignedChunk.find( entry._relativePath ); itAssigned != mapAssignedChunk.end() )
                    chunkIndex = static_cast<size_t>( std::find( listChunkName.begin(), listChunkName.end(), itAssigned->second ) - listChunkName.begin() );
            }
            listChunkPlaced[chunkIndex].push_back( Placed{ PackStreamWriter::makePathHash( entry._relativePath ), estimateEntryPackBytes( entry._relativePath, entry._sizeBytes ) } );
        }

        // 몫의 예산은 한도에서 팩 하나의 고정 크기를 뺀 것이다. 항목 크기는 팩 안 어림이다(섹터 패딩 · 색인 줄).
        const uint64     budget = split._maxPackBytes > estimateFixedPackBytes() ? split._maxPackBytes - estimateFixedPackBytes() : 0;
        vector<PackPart> listPart;
        for ( size_t chunkIndex = 0; chunkIndex < listChunkPlaced.size(); ++chunkIndex )
        {
            vector<Placed>& listPlaced = listChunkPlaced[chunkIndex];
            if ( listPlaced.empty() && chunkIndex > 0 )
                continue; // 빈 이름 청크는 팩을 만들지 않는다. 기본 청크 0 번은 늘 있다(마운트 도메인 · 정체성 색인)
            VectorUtil::sort( listPlaced, []( const Placed& left, const Placed& right )
            { return left._pathHash < right._pathHash; } );
            PackPart part;
            part._chunk = listChunkName[chunkIndex];
            for ( const Placed& placed : listPlaced )
            {
                const bool bOverLimit = part._listPathHash.empty() == false && placed._sizeBytes > budget - std::min( part._packBytes, budget );
                if ( bOverLimit )
                {
                    PackPart next;
                    next._chunk  = part._chunk;
                    next._number = part._number + 1;
                    listPart.push_back( std::move( part ) );
                    part = std::move( next );
                }
                part._listPathHash.push_back( placed._pathHash );
                part._packBytes += placed._sizeBytes;
            }
            listPart.push_back( std::move( part ) );
        }
        return listPart;
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
        const string                            normalizedCookedDir = FileUtil::normalizePath( cookedDir ).str();
        StringBuilder<constant::buffer::kMax32> stagingKey;
        stagingKey.appendFormat( "%016llx", static_cast<unsigned long long>( HashUtil::computeStringHash64( normalizedCookedDir.c_str(), normalizedCookedDir.size(), false ) ) );
        return FileUtil::joinPath( FileUtil::joinPath( FileUtil::joinPath( "packs", target ), stagingKey.view() ), packName );
    }

    bool PackCooker::shouldIncludeFile( string_view relativePath, const PackConfig& config, string_view shaderFolder, string_view buildTarget )
    {
        // 그 타깃의 런타임은 같은 표로 이 종류를 읽지 않는다(`AssetPaths::setHostTarget`).
        if ( AssetPaths::isExcludedForTarget( buildTarget, relativePath ) )
            return false;

        const string        normalized = FileUtil::normalizePath( relativePath ).str();
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
        const string        normalized = FileUtil::normalizePath( relativePath ).str();
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

    bool PackCooker::writeChunkManifests( const string& packOutputDir, const vector<string>& listDomain, const PackChunkManifest& manifest )
    {
        if ( FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( packOutputDir ) ) == false )
            return false;
        for ( const string& domain : listDomain )
        {
            StringBuilder<constant::buffer::kMax256> chunkPrefix;
            chunkPrefix.appendFormat( "%#.", PackChunkManifest::makeDomainStem( domain ) );
            StringBuilder<constant::buffer::kMax256> scenePrefix;
            scenePrefix.appendFormat( "%#/", FileUtil::normalizePath( domain ).str() );
            PackChunkManifest domainManifest;
            for ( const PackChunkFiles& files : manifest._listChunk )
            {
                if ( StringUtil::startsWith( files._name, chunkPrefix.view() ) )
                    domainManifest._listChunk.push_back( files );
            }
            for ( const PackSceneEntry& entry : manifest._listScene )
            {
                if ( StringUtil::startsWith( entry._sceneID, scenePrefix.view() ) )
                    domainManifest._listScene.push_back( entry );
            }
            // 미룬 청크가 없는 도메인도 쓴다 — 지난 쿠킹의 목록이 남아 이번에 쓰지 않은 팩을 가리키지 않게.
            const string path = FileUtil::joinPath( packOutputDir, PackChunkManifest::makeFileName( domain ) );
            if ( FileUtil::writeTextFile( FileUtil::toDiskPath( path ), PackChunkManifest::makeText( domainManifest ) ) == false )
            {
                SW_LOG_ERROR_ID( "PackCooker.ChunkManifestUnwritable", "Pack cook: the chunk manifest %# could not be written", path );
                return false;
            }
        }
        return true;
    }

    bool PackCooker::cookAllPacks( const PackConfig& engineConfig, const PackCookParams& params, const CookStore& store, uint32& outPackCount )
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
        StringBuilder<constant::buffer::kMax128> targetBuilder;
        targetBuilder.appendFormat( "%#-%#", params._buildTarget, params._shaderFolder );
        desc._target = string( targetBuilder.view() );

        // 엔진 규칙 뒤에 굽는 게임들의 규칙을 합친 사본이 이 쿠킹의 설정이다(언리얼 DefaultGame.ini 의 쿠킹 설정이 엔진 기본을 덮는 자리). 작업 범위 안에서 읽는다.
        PackConfig config = engineConfig;
        {
            CookJobDesc rulesDesc = desc;
            rulesDesc._cookerName = "PackGameRules";
            const CookJob gameRulesJob{ rulesDesc };
            if ( PackCookerInternal::mergeGameCookRules( params, listDomain, config._cookRules ) == false )
                return false;
        }

        const expected<uint64> settingsHash = PackCookerInternal::computeSettingsHash( config, params );
        if ( settingsHash.has_value() == false )
        {
            SW_LOG_ERROR_ID( "PackCooker.SettingsNotWritable", "Pack cook settings could not be written as JSON: %# (%#)", settingsHash.error().message(), settingsHash.error().detail() );
            return false;
        }
        desc._settingsHash = *settingsHash;

        PackCookerInternal::PackSelection selection;
        if ( PackCookerInternal::selectFiles( config, params, listDomain, desc, selection ) == false )
            return false;

        uint32                             failedCount = 0;
        PackCookerInternal::PackCookTiming timing;
        const Stopwatch                    totalWatch;
        const int64                        outputHashStart = store.getOutputHashNanos(); // 이 쿠킹의 몫만 센다
        unordered_set<string>              setLedger;                                    // 이 팩 폴더에 쿠커가 쓴 팩(형제 팩 정리의 근거)
        if ( PackCookerInternal::readCookedPackLedger( params._packOutputDir, setLedger ) == false )
        {
            SW_LOG_ERROR_ID( "PackCooker.PackLedgerUnreadable", "Pack cook: the cooked pack ledger in %# could not be read", params._packOutputDir );
            return false;
        }
        // 기동 때 마운트하는 청크 — 기본과 이름 붙은 패턴 청크. 씬 청크 · 공유 청크는 도메인마다의 목록(`packchunks.<도메인 줄기>.xml`)에 적어 씬을 열 때 마운트한다.
        unordered_set<string> setStartupChunk{ string{} };
        for ( const PackChunkRule& rule : config._split._listChunkRule )
        {
            setStartupChunk.insert( rule._name );
        }
        unordered_map<string, string>         mapPlacedChunk; // 담긴 에셋 id → 놓인 청크(기동 필수는 빈 글, 미룬 청크는 `<도메인 줄기>.<청크>`)
        unordered_map<string, vector<string>> mapChunkPack;   // 미루는 청크(`<도메인 줄기>.<청크>`) → 팩 파일 이름
        for ( const string& domain : listDomain )
        {
            // 나누기는 쿠킹 전에 정한다 — 목록과 디스크 크기만 본다. 목록은 여기서 한 번 고르고 몫마다 나눠 넘긴다. 목록 · 크기 묻기는 리소스 읽기가 아니라
            // 쿠킹 세션의 작업 밖 읽기 막기(`CookJob::recordReadInput` · `allowDiskRead` — `AssetIO` 읽기만 본다)에 걸리지 않는다. 몫의 경로 해시가 몫 작업의
            // 설정 해시에 들어가 파일이 생기거나 사라지거나 다른 몫의 크기가 바뀌어 경계가 옮겨지면 다시 굽는다.
            PackCookerInternal::DomainListing listing;
            if ( PackCookerInternal::collectDomainItems( config, params, domain, selection, listing ) == false )
            {
                ++failedCount;
                continue;
            }
            PackCookerInternal::logListingNotes( params, domain, listing );
            vector<PackSplitEntry> listSplitEntry;
            listSplitEntry.reserve( listing._listItem.size() );
            bool bSized = true;
            for ( const PackCookerInternal::PackItem& item : listing._listItem )
            {
                uint64 sizeBytes = 0;
                bSized           = bSized && FileUtil::tryGetFileSize( FileUtil::toDiskPath( item._absPath ), sizeBytes );
                listSplitEntry.push_back( PackSplitEntry{ item._relativePath, sizeBytes } );
            }
            if ( bSized == false )
            {
                SW_LOG_ERROR_ID( "PackCooker.PackSplitCannotSize", "Pack cook: a file of %# could not be sized to split the pack", domain );
                ++failedCount;
                continue;
            }
            unordered_map<string, string> mapAssignedChunk;
            for ( const PackCookerInternal::PackItem& item : listing._listItem )
            {
                string chunk = PackCookerInternal::findAssignedChunk( selection, domain, item );
                if ( chunk.empty() == false )
                    mapAssignedChunk.emplace( item._relativePath, std::move( chunk ) );
            }
            const expected<vector<PackPart>> split = splitIntoParts( listSplitEntry, config._split, mapAssignedChunk );
            if ( split.has_value() == false )
            {
                SW_LOG_ERROR_ID( "PackCooker.PackSplitInvalid", "Pack cook: %# could not be split - %# '%#'", domain, split.error().message(), split.error().detail() );
                ++failedCount;
                continue;
            }

            vector<string>                           listWrittenName;
            StringBuilder<constant::buffer::kMax256> chunkKey;
            for ( const PackCookerInternal::PackItem& item : listing._listItem )
            {
                for ( const PackPart& part : *split )
                {
                    // 미룬 청크는 도메인마다 따로 적는다(`<도메인 줄기>.<청크>`) — 목록 파일이 도메인 단위라 한 게임만 구워도 다른 도메인의 목록이 남는다.
                    if ( part.contains( item._pathHash ) )
                    {
                        chunkKey.clear();
                        if ( setStartupChunk.contains( part._chunk ) == false )
                            chunkKey.appendFormat( "%#.%#", PackChunkManifest::makeDomainStem( domain ), part._chunk );
                        mapPlacedChunk.emplace( PackCookerInternal::makeAssetID( domain, item._relativePath ), string( chunkKey.view() ) );
                    }
                }
            }
            for ( const PackPart& part : *split )
            {
                const string packName = makePackFileName( domain, part._chunk, part._number );
                if ( setStartupChunk.contains( part._chunk ) == false )
                {
                    chunkKey.clear();
                    chunkKey.appendFormat( "%#.%#", PackChunkManifest::makeDomainStem( domain ), part._chunk );
                    mapChunkPack[string( chunkKey.view() )].push_back( packName );
                }
                const string outputPath = FileUtil::joinPath( params._packOutputDir, packName );
                listWrittenName.push_back( packName );
                // 기록은 타깃 · 백엔드마다 따로 둔다 — 같은 팩 이름을 다른 설정으로 번갈아 베이크해도 서로의 기록을 덮지 않는다.
                const string outputID   = makeRecordID( desc._target, params._cookedDir, packName );
                CookJobDesc  partDesc   = desc;
                partDesc._settingsHash  = PackCookerInternal::computePartHash( PackCookerInternal::computeSelectionHash( desc._settingsHash, domain, selection ), part );
                const CookResult result = store.cookStreamedFile( partDesc, outputID, outputPath, [&]()
                { return PackCookerInternal::cookDomainPack( config, params, domain, outputPath, listing, part, timing ); } );
                if ( result == CookResult::Failed )
                    ++failedCount;
                else if ( result != CookResult::Skipped )
                {
                    ++outPackCount;
                    setLedger.insert( packName );
                }
            }
            // 지난 쿠킹이 더 잘게 나눴으면 이번에 쓰지 않은 형제 팩이 남는다 — 남으면 마운트가 같은 도메인의 겹침으로 거절하거나 지운 항목을 되살린다.
            if ( PackCookerInternal::removeStaleSiblingPacks( params._packOutputDir, domain, listWrittenName, setLedger ) == false )
                ++failedCount;
        }
        if ( failedCount == 0 && PackCookerInternal::writeChunkManifest( params._packOutputDir, listDomain, selection, mapPlacedChunk, mapChunkPack ) == false )
            ++failedCount;
        if ( PackCookerInternal::writeCookedPackLedger( params._packOutputDir, setLedger ) == false )
        {
            SW_LOG_ERROR_ID( "PackCooker.PackLedgerUnwritable", "Pack cook: the cooked pack ledger in %# could not be written", params._packOutputDir );
            ++failedCount;
        }
        PackCookerInternal::logSummary( outPackCount, failedCount, desc._target, totalWatch.getElapsedNanoseconds(),
                                        store.getOutputHashNanos() - outputHashStart, timing );
        return failedCount == 0;
    }
} // namespace sw
