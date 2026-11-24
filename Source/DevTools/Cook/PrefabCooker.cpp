#include "pch.h"

#include "DevTools/Cook/PrefabCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Cook/CookStore.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"

namespace sw
{
    uint32 PrefabCooker::cookAll( const AbsolutePath& sourceRoot, const AbsolutePath& cookedDir, const CookStore& store, const uint64 reflectionFingerprint, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( sourceRoot.empty() || cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "PrefabCooker.PrefabCookNeedsSource", "Prefab cook needs a source root and an output directory (--cooked-dir)." );
            return 0;
        }

        vector<AbsolutePath> listFile;
        if ( AssetPaths::collectContentFiles( sourceRoot, {}, listFile ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabCooker.PrefabCookCouldNot", "Prefab cook could not list '%#' - nothing is cooked", sourceRoot.str() );
            ++outFailedCount;
            return 0;
        }
        // 순서를 정한다 — 같은 쿠킹본을 쓰는 두 소스 가운데 어느 것이 먼저인지가 실행마다 같아야 한다.
        VectorUtil::sort( listFile );

        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot.str() ) );
        string       out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir.str() ) );
        out += '/';

        unordered_set<string> uniqueSetWrittenKey; // 중복 판정만 한다 — 출력 순서는 정렬된 `listFile` 가 정한다
        CookStats             stats;
        const CookJobDesc     desc = makeCookJobDesc( reflectionFingerprint );
        for ( const AbsolutePath& filePath : listFile )
        {
            const AbsolutePath normalized = filePath;
            const bool         bXML       = StringUtil::endsWith( normalized.str(), ".prefab.xml", true );
            const bool         bJSON      = StringUtil::endsWith( normalized.str(), ".prefab.json", true );
            if ( ( bXML || bJSON ) == false || normalized.str().size() <= root.size() + 1 )
                continue;

            // 출력은 `<cookedDir>/<소스 루트 기준 상대 경로>` 의 쿠킹본 이름이다 — 런타임 `loadPrefab` 과 같은 규칙 하나(`AssetCookPath`).
            const string                             outputID = AssetCookPath::toCookedPath( normalized.str().substr( root.size() + 1 ) );
            StringBuilder<constant::buffer::kMax512> outputPathBuilder;
            outputPathBuilder.appendFormat( "%#%#", out, outputID );
            const string outputPath = string( outputPathBuilder.view() );
            const string outputKey  = StringUtil::toLower( outputPath.c_str() );
            if ( uniqueSetWrittenKey.contains( outputKey ) )
            {
                SW_LOG_WARNING_ID( "PrefabCooker.PrefabCookWouldOverwrite", "Prefab cook: '%#' would overwrite the cooked file another source already wrote ('%#') - skipped", normalized.str(), outputPath );
                ++outFailedCount;
                continue;
            }

            const CookResult result = store.cookFile( desc, outputID, outputPath, [&]()
            {
                // 중첩 인스턴스의 원본을 읽는 캐시는 작업마다 새로 둔다 — 앞 작업이 읽어 둔 원본을 쓰면 그 읽기가 이 작업의 입력에서 빠진다.
                PrefabCache nestedPrefabs;
                PrefabAsset asset;
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                const bool                   bLoaded    = asset.loadFromDiskFile( normalized );
                const bool                   bFlattened = bLoaded && asset.isValid() && nestedPrefabs.flattenNestedInstances( asset );
                const optional<AbsolutePath> outputFile = FileUtil::requireAbsolutePath( outputPath );
                if ( outputFile.has_value() == false || bFlattened == false || asset.saveToBinaryFile( *outputFile ) == false )
                {
                    SW_LOG_WARNING_ID( "PrefabCooker.PrefabCookFailed", "Prefab cook failed for '%#'", normalized.str() );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            } );
            if ( result == CookResult::Failed )
            {
                ++outFailedCount;
                continue;
            }
            uniqueSetWrittenKey.insert( outputKey );
            stats.add( result );
        }
        SW_LOG_INFO_ID( "PrefabCooker.PrefabCookReusedDDC", "Prefab cook: %# reused from the DDC, %# cooked", stats._reusedCount, stats._cookedCount );
        return stats.getWrittenCount();
    }

    CookJobDesc PrefabCooker::makeCookJobDesc( const uint64 reflectionFingerprint )
    {
        CookJobDesc desc;
        desc._cookerName      = "Prefab";
        desc._codeFingerprint = CookCodeFingerprint::find( "Prefab" );
        desc._target          = "Any";
        desc._settingsHash    = reflectionFingerprint; // 덮어쓰기 · 변형 합치기가 타입 표(필수 · 열쇠 칸)를 본다
        return desc;
    }
} // namespace sw
