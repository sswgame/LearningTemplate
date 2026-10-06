#include "pch.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"

#include "Core/File/FileUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct FileLocalSlotStorageInternal
        {
            static constexpr const utf8* kTempMarker = ".swls.tmp";

            static bool isSlotInfoBefore( const LocalSlotInfo& left, const LocalSlotInfo& right ) { return left._slot < right._slot; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FileLocalSlotStorage::FileLocalSlotStorage( string_view rootDirectory )
        : _rootDirectory{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( rootDirectory ) ) }
    {
        (void)FileUtil::ensureDirectoryExists( _rootDirectory );
        vector<string> listTempPath;
        (void)FileUtil::forEachDirectoryEntry( _rootDirectory, true, [&listTempPath]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory == false && entry._path.find( FileLocalSlotStorageInternal::kTempMarker ) != string_view::npos )
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
        return FileUtil::readFile( path, outEnvelopeBytes ) ? LocalStoreResult::Ok : LocalStoreResult::IoError;
    }

    LocalStoreResult FileLocalSlotStorage::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return LocalStoreResult::IoError;
        return FileUtil::writeFile( path, envelopeBytes.data(), envelopeBytes.size() ) ? LocalStoreResult::Ok : LocalStoreResult::IoError;
    }

    LocalStoreResult FileLocalSlotStorage::eraseSlot( const string& slot )
    {
        const string path = makeSlotPath( slot );
        if ( FileUtil::isRegularFile( path ) == false )
            return LocalStoreResult::NotFound;
        return FileUtil::removeFile( path ) ? LocalStoreResult::Ok : LocalStoreResult::IoError;
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
            LocalSlotInfo& info = listSlotInfo.emplace_back();
            info._slot          = string( slot );
            info._byteCount     = static_cast<int64>( FileUtil::getFileSize( entry._path ) );
            int64 ticks         = 0;
            if ( FileUtil::getFileWriteTime( entry._path, ticks ) )
                info._writtenAtMs = ticks / ( FileUtil::kFileTimeTicksPerSecond / 1000 );
            return true;
        } );
        if ( bListed == false )
            return LocalStoreResult::IoError;
        std::sort( outListSlotInfo.begin() + static_cast<ptrdiff_t>( firstIndex ), outListSlotInfo.end(), &FileLocalSlotStorageInternal::isSlotInfoBefore );
        return LocalStoreResult::Ok;
    }

    string FileLocalSlotStorage::makeSlotPath( const string& slot ) const { return FileUtil::joinPath( _rootDirectory, slot + kSlotExtension ); }
} // namespace sw
