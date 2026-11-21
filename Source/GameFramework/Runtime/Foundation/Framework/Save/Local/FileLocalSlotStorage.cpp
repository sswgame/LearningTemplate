#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/FileLocalSlotStorage.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        constexpr const utf8* kLocalSlotTempMarker = ".swls.tmp";
    } // namespace
} // namespace sw

namespace sw
{
    FileLocalSlotStorage::FileLocalSlotStorage( string_view rootDirectory )
        : _rootDirectory{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( rootDirectory ) ) }
    {
        (void)FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( _rootDirectory ) ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
        vector<string> listTempPath;
        (void)FileUtil::forEachDirectoryEntry( FileUtil::toDiskPath( _rootDirectory ), true, [&listTempPath]( const DirectoryEntry& entry )
        {
            // 쓴 프로세스가 살아 있는 임시 파일은 다른 인스턴스가 쓰는 중이다 — 지우지 않는다
            if ( entry._bDirectory == false && entry._path.find( kLocalSlotTempMarker ) != string_view::npos && FileUtil::isAbandonedWriteTemp( entry._path ) )
                listTempPath.emplace_back( entry._path );
            return true;
        } );
        for ( const string& tempPath : listTempPath )
        {
            if ( FileUtil::tryRemoveFile( FileUtil::toDiskPath( tempPath ) ) )
                SW_LOG_WARNING_ID( "LocalStore.LeftoverTempRemoved", "Removed a leftover local store temp file (an interrupted write): %#", tempPath.c_str() );
        }
    }

    LocalStoreResult FileLocalSlotStorage::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::isRegularFile( FileUtil::toDiskPath( path ) ) == false )
            return LocalStoreResult::NotFound;
        return FileUtil::readFile( FileUtil::toDiskPath( path ), outEnvelopeBytes ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::ensureParentDirectoryExists( FileUtil::toDiskPath( path ) ) == false )
            return LocalStoreResult::IOError;
        // 지금 파일을 한 세대 남긴다(언리얼 SaveGame 백업 슬롯 자리). 못 남기면 쓰지 않는다 — 다음 쓰기가 덮어 둘 다 잃는 일을 막는다.
        StringBuilder<constant::buffer::kMax512> previousPath;
        previousPath.appendFormat( "%#%#", path, kPreviousGenerationSuffix );
        if ( FileUtil::isRegularFile( FileUtil::toDiskPath( path ) ) && FileUtil::copyFile( FileUtil::toDiskPath( path ), FileUtil::toDiskPath( previousPath.view() ) ) == false )
            return LocalStoreResult::IOError;
        return FileUtil::writeFileDurable( FileUtil::toDiskPath( path ), envelopeBytes.data(), envelopeBytes.size() ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::readPreviousSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        StringBuilder<constant::buffer::kMax512> path;
        path.appendFormat( "%#%#", makeSlotPath( slot ), kPreviousGenerationSuffix );
        if ( FileUtil::isRegularFile( FileUtil::toDiskPath( path.view() ) ) == false )
            return LocalStoreResult::NotFound;
        return FileUtil::readFile( FileUtil::toDiskPath( path.view() ), outEnvelopeBytes ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::eraseSlot( const string& slot )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::isRegularFile( FileUtil::toDiskPath( path ) ) == false )
            return LocalStoreResult::NotFound;
        // 직전 세대도 지운다 — 남기면 지운 슬롯이 다음 쓰기의 깨진 읽기에서 되살아난다
        StringBuilder<constant::buffer::kMax512> previousPath;
        previousPath.appendFormat( "%#%#", path, kPreviousGenerationSuffix );
        if ( FileUtil::isRegularFile( FileUtil::toDiskPath( previousPath.view() ) ) && FileUtil::removeFile( FileUtil::toDiskPath( previousPath.view() ) ) == false )
            return LocalStoreResult::IOError;
        return FileUtil::removeFile( FileUtil::toDiskPath( path ) ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        const size_t           firstIndex   = outListSlotInfo.size();
        const size_t           rootSize     = _rootDirectory.size() + 1;
        const string           extension    = kSlotExtension;
        vector<LocalSlotInfo>& listSlotInfo = outListSlotInfo;
        const bool             bListed      = FileUtil::forEachDirectoryEntry( FileUtil::toDiskPath( _rootDirectory ), true, [&]( const DirectoryEntry& entry )
                         {
            const bool bSlotFile = entry._bDirectory == false && entry._path.size() > rootSize + extension.size() &&
                                   entry._path.substr( entry._path.size() - extension.size() ) == extension;
            if ( bSlotFile == false )
                return true;
            const string_view slot = entry._path.substr( rootSize, entry._path.size() - rootSize - extension.size() );
            if ( slot.substr( 0, groupPrefix.size() ) != groupPrefix || ILocalStore::isValidSlotName( slot ) == false )
                return true;
            LocalSlotInfo& slotInfo = listSlotInfo.emplace_back();
            slotInfo._slot          = string( slot );
            uint64 byteCount        = 0;
            if ( FileUtil::tryGetFileSize( FileUtil::toDiskPath( entry._path ), byteCount ) )
                slotInfo._byteCount = static_cast<int64>( byteCount );
            int64 ticks = 0;
            if ( FileUtil::getFileWriteTime( FileUtil::toDiskPath( entry._path ), ticks ) )
                slotInfo._writtenAtMs = ticks / ( FileUtil::kFileTimeTicksPerSecond / constant::time::kMillisecondsPerSecond );
            return true;
        } );
        if ( bListed == false )
            return LocalStoreResult::IOError;
        VectorUtil::sortRange( outListSlotInfo.begin() + static_cast<ptrdiff_t>( firstIndex ), outListSlotInfo.end(), Ascending<&LocalSlotInfo::_slot>{} );
        return LocalStoreResult::Ok;
    }

    string FileLocalSlotStorage::makeSlotPath( const string& slot ) const
    {
        StringBuilder<constant::buffer::kMax256> slotFile;
        slotFile.appendFormat( "%#%#", slot, kSlotExtension );
        return FileUtil::joinPath( _rootDirectory, slotFile.view() );
    }
} // namespace sw
