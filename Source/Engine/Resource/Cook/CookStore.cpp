#include "pch.h"

#include "Engine/Resource/Cook/CookStore.h"

#include "Core/Common/Defines.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    SW_LOG_CALLER( "CookStore" );

    CookStore::CookStore( string recordRootPath, string ddcRootPath )
        : _recordRootPath{ std::move( recordRootPath ) }
        , _derivedDataCache{ std::move( ddcRootPath ) }
    {
    }

    CookStore CookStore::makeLocal()
    {
        const string savedPath = FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), path::kSavedFolder );
        return CookStore{ FileUtil::joinPath( savedPath, "CookRecord" ), DerivedDataCache::makeLocalRootPath() };
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
            SW_LOG_INFO( "Cook: '%#' is cooked again - %#%#", outputID, pChanged != nullptr ? "input changed: " : "cooker version or settings changed",
                         pChanged != nullptr ? pChanged->_resourceID : string{} );
            return false;
        }
        // 출력 파일이 이미 기록의 바이트면 다시 쓰지 않는다 — 무변경 재쿠킹이 파일(큰 팩)을 건드리지 않는다.
        vector<uint8> bytes;
        const bool    bOutputCurrent = FileUtil::isRegularFile( outputPath ) && FileUtil::readFile( outputPath, bytes ) && ContentHash::computeBytes( bytes ) == record._outputHash;
        if ( bOutputCurrent )
            return true;
        bytes.clear();
        if ( _derivedDataCache.find( record._key, bytes ) == false )
        {
            SW_LOG_INFO( "Cook: '%#' has a fresh record but no DDC entry - cooked again", outputID );
            return false;
        }
        if ( ContentHash::computeBytes( bytes ) != record._outputHash )
        {
            SW_LOG_WARNING( "Cook: the DDC entry of '%#' does not match its record - cooked again", outputID );
            return false;
        }
        if ( FileUtil::ensureParentDirectoryExists( outputPath ) == false || FileUtil::writeFile( outputPath, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_WARNING( "Cook: could not write '%#' from the DDC - cooked again", outputPath );
            return false;
        }
        return true;
    }

    CookOutcome CookStore::finishCook( const CookJob& job, CookStep step, string_view outputID, string_view outputPath ) const
    {
        if ( step == CookStep::Skipped )
            return CookOutcome::Skipped;
        if ( step == CookStep::Failed )
            return CookOutcome::Failed;
        // 입력이 없는 기록은 무엇이 바뀌어도 새것이다 — 쿠커가 `ResourceIO` 를 거치지 않고 읽었다는 뜻이므로 실패로 멈춘다.
        if ( job.getInputs().empty() )
        {
            SW_LOG_ERROR( "Cook: '%#' recorded no input - its cooker read its sources around ResourceIO, so a change would not cook it again", outputID );
            return CookOutcome::Failed;
        }
        vector<uint8> bytes;
        if ( FileUtil::readFile( outputPath, bytes ) == false )
        {
            SW_LOG_ERROR( "Cook: '%#' was reported written but cannot be read back", outputPath );
            return CookOutcome::Failed;
        }
        const CookRecord record = CookRecord::make( job, bytes );
        if ( _derivedDataCache.store( record._key, bytes ) == false || record.write( makeRecordPath( outputID ) ) == false )
        {
            SW_LOG_ERROR( "Cook: could not store '%#' in the DDC or write its record", outputID );
            return CookOutcome::Failed;
        }
        return CookOutcome::Cooked;
    }

    void CookStore::clearAssetCachesForJob()
    {
        engine::getSubsystem<AssetManager>().clearAssetCaches();
    }
} // namespace sw
