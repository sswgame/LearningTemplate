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
    CookStore::CookStore( string recordRootPath, string ddcRootPath )
        : _recordRootPath{ std::move( recordRootPath ) }
        , _derivedDataCache{ std::move( ddcRootPath ) }
        , _outputHashNanos{ 0 }
    {
    }

    CookStore CookStore::makeLocal()
    {
        return CookStore{ FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), "CookRecord" ), DerivedDataCache::makeLocalRootPath() };
    }

    string CookStore::makeRecordPath( string_view outputID ) const
    {
        return FileUtil::joinPath( _recordRootPath, string( outputID ) + ".cookrec" );
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
        vector<uint8> bytes;
        const bool    bOutputCurrent = FileUtil::isRegularFile( outputPath ) && FileUtil::readFile( outputPath, bytes ) && hashOutput( bytes ) == record._outputHash;
        if ( bOutputCurrent )
            return true;
        bytes.clear();
        if ( _derivedDataCache.find( record._key, bytes ) == false )
        {
            SW_LOG_INFO_ID( "CookStore.CookFreshRecordBut", "Cook: '%#' has a fresh record but no DDC entry - cooked again", outputID );
            return false;
        }
        if ( hashOutput( bytes ) != record._outputHash )
        {
            SW_LOG_WARNING_ID( "CookStore.CookDDCEntryDoes", "Cook: the DDC entry of '%#' does not match its record - cooked again", outputID );
            return false;
        }
        if ( FileUtil::ensureParentDirectoryExists( outputPath ) == false || FileUtil::writeFile( outputPath, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_WARNING_ID( "CookStore.CookCouldNotWrite", "Cook: could not write '%#' from the DDC - cooked again", outputPath );
            return false;
        }
        return true;
    }

    uint64 CookStore::hashOutput( const vector<uint8>& bytes ) const
    {
        const Stopwatch watch;
        const uint64    hash = ContentHash::computeBytes( bytes );
        _outputHashNanos += watch.getElapsedNanoseconds();
        return hash;
    }

    CookResult CookStore::finishCook( const CookJob& job, CookStep step, string_view outputID, string_view outputPath ) const
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
        vector<uint8> bytes;
        if ( FileUtil::readFile( outputPath, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "CookStore.CookReportedWrittenBut", "Cook: '%#' was reported written but cannot be read back", outputPath );
            return CookResult::Failed;
        }
        // 출력 파일은 쿠커가 쓰므로 다시 읽는 것은 DDC 저장에 어차피 필요하다. 해시는 그 바이트를 한 번만 훑는다(`hashOutput` 이 시간도 잰다).
        const CookRecord record = CookRecord::make( job, hashOutput( bytes ) );
        if ( _derivedDataCache.store( record._key, bytes ) == false || record.write( makeRecordPath( outputID ) ) == false )
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
        if ( FileUtil::isDirectory( recordRoot ) && FileUtil::collectFiles( recordRoot, kRecordSuffix, listRecord, true ) == false )
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
            if ( FileUtil::isRegularFile( outputPath ) == false ) // 팩 기록처럼 쿠킹 폴더에 출력이 없는 것은 이 정리의 몫이 아니다
                continue;
            if ( FileUtil::removeFile( outputPath ) )
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
