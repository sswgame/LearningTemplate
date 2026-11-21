#include "pch.h"

#include "Engine/Asset/Cook/CookStore.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    CookStore::CookStore( string recordRootPath, DerivedDataCache derivedDataCache )
        : _recordRootPath{ std::move( recordRootPath ) }
        , _derivedDataCache{ std::move( derivedDataCache ) }
        , _outputHashNanos{ 0 }
    {
    }

    expected<CookStore> CookStore::makeLocal()
    {
        expected<DerivedDataCache> derivedDataCache = DerivedDataCache::loadFromConfig();
        if ( derivedDataCache.has_value() == false )
            return unexpected( derivedDataCache.error() );
        return CookStore{ FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), "CookRecord" ), std::move( *derivedDataCache ) };
    }

    string CookStore::makeRecordPath( string_view outputID ) const
    {
        StringBuilder<constant::buffer::kMax512> builder;
        builder.appendFormat( "%#.cookrec", outputID );
        return FileUtil::joinPath( _recordRootPath, builder.view() );
    }

    bool CookStore::reuseFreshOutput( const CookJobDesc& desc, string_view outputID, string_view outputPath ) const
    {
        CookRecord record;
        if ( CookRecord::read( makeRecordPath( outputID ), record ) == false )
            return false;
        if ( record.isFresh( desc ) == false )
        {
            const CookInput* pChanged = record.findChangedInput();
            SW_LOG_INFO_ID( "CookStore.CookCookedAgain", "Cook: '%#' is cooked again - %#%#", outputID, pChanged != nullptr ? "input changed: " : "cooker version or settings changed",
                            pChanged != nullptr ? pChanged->_source : string{} );
            return false;
        }
        // 출력 파일이 이미 기록의 바이트면 다시 쓰지 않는다 — 무변경 재쿠킹이 파일(큰 팩)을 건드리지 않는다.
        // 해시는 내용 해시 캐시로 구한다 — 도장(크기 · 시각)이 지난번과 같으면 큰 팩을 다시 읽지 않는다.
        const bool bOutputCurrent = FileUtil::isRegularFile( FileUtil::toDiskPath( outputPath ) ) && hashOutputFile( outputPath ) == record._outputHash;
        if ( bOutputCurrent )
            return true;
        // DDC 의 출력을 출력 옆 임시 파일로 흘려 쓰고(DDC 가 본문 해시를 본다), 그 해시가 기록과 같을 때만 출력과 바꿔 끼운다 — 다르면 출력 자리를 건드리지 않는다(메모리로 들지도 않는다).
        const string tempPath = FileUtil::makeWriteTempPath( outputPath );
        if ( _derivedDataCache.find( record._key, CookJob::makeKeyDescription( desc, record._listInput ), tempPath ) == false )
        {
            (void)FileUtil::tryRemoveFile( FileUtil::toDiskPath( tempPath ) );
            SW_LOG_INFO_ID( "CookStore.CookFreshRecordBut", "Cook: '%#' has a fresh record but no DDC entry (or it could not be copied) - cooked again", outputID );
            return false;
        }
        if ( hashOutputFile( tempPath ) != record._outputHash )
        {
            (void)FileUtil::tryRemoveFile( FileUtil::toDiskPath( tempPath ) );
            SW_LOG_WARNING_ID( "CookStore.CookDDCEntryDoes", "Cook: the DDC entry of '%#' does not match its record - cooked again", outputID );
            return false;
        }
        if ( FileUtil::replaceFile( FileUtil::toDiskPath( tempPath ), FileUtil::toDiskPath( outputPath ) ) == false )
        {
            SW_LOG_WARNING_ID( "CookStore.CookCouldNotWrite", "Cook: could not write '%#' from the DDC - cooked again", outputPath );
            return false;
        }
        return true;
    }

    uint64 CookStore::hashOutputFile( string_view outputPath ) const
    {
        const Stopwatch watch;
        const uint64    hash = ContentHash::computeFile( outputPath );
        _outputHashNanos += watch.getElapsedNanoseconds();
        return hash;
    }

    optional<CookResult> CookStore::validateFinishedJob( const CookJob& job, CookStep step, string_view outputID )
    {
        if ( step == CookStep::Skipped )
            return CookResult::Skipped;
        if ( step == CookStep::Failed )
            return CookResult::Failed;
        // 입력이 없는 기록은 무엇이 바뀌어도 새것이다 — 쿠커가 `AssetIO` 를 거치지 않고 읽었다는 뜻이므로 실패로 멈춘다.
        if ( job.getInputs().empty() )
        {
            SW_LOG_ERROR_ID( "Cook.NoInputRecorded", "Cook: '%#' recorded no input - its cooker read its sources around AssetIO, so a change would not cook it again", outputID );
            return CookResult::Failed;
        }
        return nullopt;
    }

    CookResult CookStore::finishStreamedCook( const CookJob& job, const CookStreamedStep& step, string_view outputID, string_view outputPath ) const
    {
        const optional<CookResult> finished = validateFinishedJob( job, step._step, outputID );
        if ( finished.has_value() )
            return *finished;
        const CookRecord record = CookRecord::make( job, step._outputHash );
        if ( _derivedDataCache.store( record._key, job.makeKeyDescription(), outputPath ) == false || record.write( makeRecordPath( outputID ) ) == false )
        {
            SW_LOG_ERROR_ID( "CookStore.CookCouldNotStore2", "Cook: could not store '%#' in the DDC or write its record", outputID );
            return CookResult::Failed;
        }
        return CookResult::Cooked;
    }

    CookResult CookStore::finishCook( const CookJob& job, CookStep step, string_view outputID, string_view outputPath ) const
    {
        const optional<CookResult> finished = validateFinishedJob( job, step, outputID );
        if ( finished.has_value() )
            return *finished;
        // 출력 해시는 파일을 조각으로 읽어 센다(내용 해시 캐시가 그 값을 남긴다). DDC 에는 파일을 복사한다 — 출력을 한 벡터로 들지 않는다.
        const uint64 outputHash = hashOutputFile( outputPath );
        if ( outputHash == 0 )
        {
            SW_LOG_ERROR_ID( "CookStore.CookReportedWrittenBut", "Cook: '%#' was reported written but cannot be read back", outputPath );
            return CookResult::Failed;
        }
        const CookRecord record = CookRecord::make( job, outputHash );
        if ( _derivedDataCache.store( record._key, job.makeKeyDescription(), outputPath ) == false || record.write( makeRecordPath( outputID ) ) == false )
        {
            SW_LOG_ERROR_ID( "CookStore.CookCouldNotStore", "Cook: could not store '%#' in the DDC or write its record", outputID );
            return CookResult::Failed;
        }
        return CookResult::Cooked;
    }

    CookResult CookStore::record( string_view outputID, CookResult result ) const
    {
        _stats.add( result );
        if ( result != CookResult::Skipped )
            _setRequestedID.insert( string( outputID ) );
        return result;
    }

    uint32 CookStore::removeUnrequestedOutputs( string_view cookedDir, uint32& outFailedCount ) const
    {
        outFailedCount = 0;
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "CookStore.CookRemovingUnrequestedOutputs", "Cook: removing unrequested outputs needs the cooked directory" );
            ++outFailedCount;
            return 0;
        }
        constexpr string_view kRecordSuffix = ".cookrec";
        const string          recordRoot    = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( _recordRootPath ) );
        vector<string>        listRecord;
        // 기록 폴더가 아직 없으면 지울 옛 출력도 없다. 도중에 멈춘 목록이면 지울 것을 빠뜨리므로 실패로 센다(경로는 collectFiles 가 알린다).
        if ( FileUtil::isDirectory( FileUtil::toDiskPath( recordRoot ) ) && FileUtil::collectFiles( FileUtil::toDiskPath( recordRoot ), kRecordSuffix, listRecord, true ) == false )
            ++outFailedCount;
        uint32 removedCount = 0;
        for ( const string& recordPath : listRecord )
        {
            const string normalized = FileUtil::normalizeSeparators( recordPath );
            if ( normalized.size() <= recordRoot.size() + 1 + kRecordSuffix.size() )
                continue;
            const string outputID = normalized.substr( recordRoot.size() + 1, normalized.size() - recordRoot.size() - 1 - kRecordSuffix.size() );
            if ( _setRequestedID.contains( outputID ) )
                continue;
            const string outputPath = FileUtil::joinPath( cookedDir, outputID );
            if ( FileUtil::isRegularFile( FileUtil::toDiskPath( outputPath ) ) == false ) // 팩 기록처럼 쿠킹 폴더에 출력이 없는 것은 이 정리의 몫이 아니다
                continue;
            if ( FileUtil::removeFile( FileUtil::toDiskPath( outputPath ) ) )
            {
                SW_LOG_INFO_ID( "CookStore.CookRemovedNoCooker", "Cook: removed '%#' - no cooker requested it this run (its source is gone or is no longer cooked)", outputID );
                ++removedCount;
            }
            else
            {
                SW_LOG_ERROR_ID( "CookStore.CookCouldNotRemove", "Cook: could not remove the unrequested output '%#'", outputPath );
                ++outFailedCount;
            }
        }
        return removedCount;
    }

    void CookStore::clearAssetCachesForJob()
    {
        engine::getSubsystem<AssetManager>().clearAssetCaches();
    }
} // namespace sw
