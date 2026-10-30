#include "pch.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"

#include "Core/Container/CompareUtil.h"
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
        (void)FileUtil::ensureDirectoryExists( _rootDirectory ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
        vector<string> listTempPath;
        (void)FileUtil::forEachDirectoryEntry( _rootDirectory, true, [&listTempPath]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory == false && entry._path.find( kLocalSlotTempMarker ) != string_view::npos )
                listTempPath.emplace_back( entry._path );
            return true;
        } );
        for ( const string& tempPath : listTempPath )
        {
            if ( FileUtil::tryRemoveFile( tempPath ) )
                SW_LOG_WARNING( "Removed a leftover local store temp file (an interrupted write): %#", tempPath.c_str() );
        }
    }

    LocalStoreResult FileLocalSlotStorage::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::isRegularFile( path ) == false )
            return LocalStoreResult::NotFound;
        return FileUtil::readFile( path, outEnvelopeBytes ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return LocalStoreResult::IOError;
        return FileUtil::writeFile( path, envelopeBytes.data(), envelopeBytes.size() ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::eraseSlot( const string& slot )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::isRegularFile( path ) == false )
            return LocalStoreResult::NotFound;
        return FileUtil::removeFile( path ) ? LocalStoreResult::Ok : LocalStoreResult::IOError;
    }

    LocalStoreResult FileLocalSlotStorage::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        const size_t           firstIndex   = outListSlotInfo.size();
        const size_t           rootSize     = _rootDirectory.size() + 1;
        const string           extension    = kSlotExtension;
        vector<LocalSlotInfo>& listSlotInfo = outListSlotInfo;
        const bool             bListed      = FileUtil::forEachDirectoryEntry( _rootDirectory, true, [&]( const DirectoryEntry& entry )
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
            if ( FileUtil::tryGetFileSize( entry._path, byteCount ) )
                slotInfo._byteCount = static_cast<int64>( byteCount );
            int64 ticks = 0;
            if ( FileUtil::getFileWriteTime( entry._path, ticks ) )
                slotInfo._writtenAtMs = ticks / ( FileUtil::kFileTimeTicksPerSecond / constant::kMillisecondsPerSecond );
            return true;
        } );
        if ( bListed == false )
            return LocalStoreResult::IOError;
        std::sort( outListSlotInfo.begin() + static_cast<ptrdiff_t>( firstIndex ), outListSlotInfo.end(), Ascending<&LocalSlotInfo::_slot>{} );
        return LocalStoreResult::Ok;
    }

    string FileLocalSlotStorage::makeSlotPath( const string& slot ) const
    {
        return FileUtil::joinPath( _rootDirectory, slot + kSlotExtension );
    }
} // namespace sw
