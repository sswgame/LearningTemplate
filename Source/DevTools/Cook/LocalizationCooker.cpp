#include "pch.h"

#include "DevTools/Cook/LocalizationCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Cook/CookStore.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    uint32 LocalizationCooker::cookAll( const AbsolutePath& sourceRoot, const AbsolutePath& cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( sourceRoot.empty() || cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "LocalizationCooker.NeedsSourceAndOutput", "Localization cook needs a source root and an output directory (--cooked-dir)." );
            ++outFailedCount;
            return 0;
        }
        vector<AbsolutePath> listFile;
        if ( AssetPaths::collectContentFiles( sourceRoot, ".xml", listFile ) == false )
        {
            SW_LOG_ERROR_ID( "LocalizationCooker.CouldNotList", "Localization cook could not list '%#' - nothing is cooked", sourceRoot.str() );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listFile ); // 출력 순서가 실행마다 같다

        const string      root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot.str() ) );
        const string      out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir.str() ) );
        const CookJobDesc desc = makeCookJobDesc();
        CookStats         stats;
        for ( const AbsolutePath& filePath : listFile )
        {
            const AbsolutePath normalized = filePath;
            if ( StringUtil::endsWith( normalized.str(), LocalizationProject::kExtension, true ) == false || normalized.str().size() <= root.size() + 1 )
                continue;
            const string projectID = normalized.str().substr( root.size() + 1 );

            // 어느 문화권을 구울지는 작업 밖에서 정한다(디스크 입구 — 세션이 막는 리소스 읽기가 아니다). 목록이 바뀌면 사라진 문화권의 출력은 쿠킹 끝의 정리가 지운다.
            LocalizationProject project;
            if ( const expected<void> read = project.loadFromFile( normalized ); read.has_value() == false )
            {
                SW_LOG_ERROR_ID( "LocalizationCooker.ProjectNotRead", "Localization cook could not read project '%#': %# (%#)", normalized.str(), read.error().message(), read.error().detail() );
                ++outFailedCount;
                continue;
            }
            for ( const string& culture : project.collectCookedCultures() )
            {
                const string                             outputID = project.makeCookedTablePath( projectID, culture );
                StringBuilder<constant::buffer::kMax512> outputPathBuilder;
                outputPathBuilder.appendFormat( "%#/%#", out, outputID );
                const AbsolutePath outputPath = FileUtil::toDiskPath( outputPathBuilder.view() );
                const CookResult   result     = store.cookFile( desc, outputID, outputPath.str(), [&]()
                      {
                    vector<uint8> bytes;
                    if ( cookCulture( AssetID{ projectID }, culture, bytes ) == false )
                        return CookStep::Failed;
                    const bool bWritten = FileUtil::ensureDirectoryExists( outputPath.getParent() ) && FileUtil::writeFile( outputPath, bytes.data(), bytes.size() );
                    if ( bWritten == false )
                    {
                        SW_LOG_ERROR_ID( "LocalizationCooker.CouldNotWrite", "Localization cook could not write '%#'", outputPath.str() );
                        return CookStep::Failed;
                    }
                    return CookStep::Written;
                } );
                outFailedCount += result == CookResult::Failed ? 1u : 0u;
                stats.add( result );
            }
        }
        SW_LOG_INFO_ID( "LocalizationCooker.Summary", "Localization cook: %# reused from the DDC, %# cooked, %# failed", stats._reusedCount, stats._cookedCount, outFailedCount );
        return stats.getWrittenCount();
    }

    bool LocalizationCooker::cookCulture( const AssetID& projectID, string_view culture, vector<uint8>& outBytes )
    {
        LocalizationManager manager; // 프로젝트 하나만 — 엔진 관리자의 언어 · 낱개 표가 섞이지 않는다
        if ( manager.mountProject( projectID, LocalizationScope::Game ) == false )
        {
            SW_LOG_ERROR_ID( "LocalizationCooker.ProjectNotMounted", "Localization cook: project '%#' is not (fully) loaded - culture '%#' is not cooked", projectID.view(), culture );
            return false;
        }
        if ( manager.cookBinaryTable( culture, outBytes ) == false )
        {
            // 해시 충돌은 cookBinaryTable 이 이미 알렸다. 그 밖은 화면에 나올 글이 하나도 없는 문화권이다 — 출시 문화권인데 빈 표를 싣지 않는다.
            SW_LOG_ERROR_ID( "LocalizationCooker.CultureNotCooked", "Localization cook: culture '%#' of project '%#' has no shown text (or a key hash collision above)", culture, projectID.view() );
            return false;
        }
        return true;
    }

    CookJobDesc LocalizationCooker::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName      = "Localization";
        desc._codeFingerprint = CookCodeFingerprint::find( "Localization" );
        desc._target          = "Any";
        desc._settingsHash    = 0; // 쿠킹 설정이 없다 — 무엇을 굽는지는 프로젝트 파일(작업의 입력)이 정한다
        return desc;
    }
} // namespace sw
