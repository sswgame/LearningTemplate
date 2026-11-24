#include "pch.h"

#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    LocalPlatformServices::LocalPlatformServices( const AbsolutePath& folder )
        : _folder{ folder }
        , _listUnlocked{}
        , _mapStat{}
        , _mapPresence{}
        , _bAchievementsDirty{ SW_FALSE }
        , _bStatsDirty{ SW_FALSE }
        , _bPresenceDirty{ SW_FALSE }
        , _bAchievementsKept{ SW_TRUE }
        , _bStatsKept{ SW_TRUE }
        , _reserved{ 0 }
    {
        string                 text;
        const OptionalFileRead achievementsRead = FileUtil::readOptionalTextFile( _folder.join( kAchievementsFileName ), text );
        if ( achievementsRead == OptionalFileRead::Failed )
        {
            _bAchievementsKept = SW_FALSE;
            reportUnreadable( kAchievementsFileName );
        }
        if ( achievementsRead == OptionalFileRead::Read )
        {
            StringUtil::forEachLine( text, [this]( string_view line )
            {
                const string_view achievementID = StringUtil::trim( line );
                if ( achievementID.empty() == false && VectorUtil::contains( _listUnlocked, string( achievementID ) ) == false )
                    _listUnlocked.push_back( string( achievementID ) );
            } );
        }
        if ( readKeyValueFile( kStatsFileName, _mapStat ) == false )
        {
            _bStatsKept = SW_FALSE;
            reportUnreadable( kStatsFileName );
        }
    }

    bool LocalPlatformServices::unlockAchievement( string_view achievementID )
    {
        if ( isPlainKey( achievementID ) == false )
            return false;
        if ( VectorUtil::contains( _listUnlocked, string( achievementID ) ) )
            return true; // 이미 풀렸어도 true 다(계약)
        _listUnlocked.push_back( string( achievementID ) );
        _bAchievementsDirty = SW_TRUE;
        return true;
    }

    bool LocalPlatformServices::setStat( string_view statID, int64 value )
    {
        if ( isPlainKey( statID ) == false )
            return false;
        _mapStat[string( statID )] = to_string( value );
        _bStatsDirty               = SW_TRUE;
        return true;
    }

    bool LocalPlatformServices::writeCloudFile( string_view fileName, const vector<uint8>& bytes )
    {
        if ( isPlainFileName( fileName ) == false )
            return false;
        const AbsolutePath path = _folder.join( kCloudFolderName ).join( fileName );
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeFileDurable( path, bytes.data(), bytes.size() );
    }

    bool LocalPlatformServices::readCloudFile( string_view fileName, vector<uint8>& outBytes )
    {
        outBytes.clear();
        if ( isPlainFileName( fileName ) == false )
            return false;
        const AbsolutePath path = _folder.join( kCloudFolderName ).join( fileName );
        if ( FileUtil::exists( path ) == false )
            return false;
        if ( FileUtil::readFile( path, outBytes ) == false )
        {
            outBytes.clear();
            return false;
        }
        return true;
    }

    bool LocalPlatformServices::setRichPresence( string_view key, string_view value )
    {
        if ( isPlainKey( key ) == false || value.find_first_of( "\r\n" ) != string_view::npos )
            return false;
        _mapPresence[string( key )] = string( value );
        _bPresenceDirty             = SW_TRUE;
        return true;
    }

    bool LocalPlatformServices::requestAuthTicket( string_view identity, vector<uint8>& outTicket )
    {
        (void)identity;
        outTicket.clear();
        return false; // 로컬 플랫폼에는 서버가 확인할 계정이 없다(파일 설명)
    }

    void LocalPlatformServices::update()
    {
        if ( _bAchievementsDirty == SW_TRUE && _bAchievementsKept == SW_TRUE )
        {
            StringBuilder<constant::buffer::kMax1024> text;
            for ( const string& achievementID : _listUnlocked )
            {
                text.appendFormat( "%#\n", achievementID );
            }
            const AbsolutePath path   = _folder.join( kAchievementsFileName );
            const bool         bSaved = FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFileDurable( path, text.view() );
            if ( bSaved == false )
                SW_LOG_WARNING_ID( "LocalPlatformServices.AchievementsNotSaved", "Local platform: '%#' could not be written - tried again on the next update", path.c_str() );
            _bAchievementsDirty = bSaved ? SW_FALSE : SW_TRUE;
        }
        if ( _bStatsDirty == SW_TRUE && _bStatsKept == SW_TRUE )
            _bStatsDirty = writeKeyValueFile( kStatsFileName, _mapStat ) ? SW_FALSE : SW_TRUE;
        if ( _bPresenceDirty == SW_TRUE )
            _bPresenceDirty = writeKeyValueFile( kPresenceFileName, _mapPresence ) ? SW_FALSE : SW_TRUE;
    }

    int64 LocalPlatformServices::getStat( string_view statID ) const
    {
        const auto mapIter = _mapStat.find( string( statID ) );
        int64      value   = 0;
        if ( mapIter == _mapStat.end() || StringUtil::parseInt64( mapIter->second, value ) == false )
            return 0;
        return value;
    }

    bool LocalPlatformServices::isPlainFileName( string_view fileName )
    {
        const bool bSeparator = fileName.find_first_of( "/\\:" ) != string_view::npos;
        return fileName.empty() == false && bSeparator == false && fileName.find( ".." ) == string_view::npos;
    }

    bool LocalPlatformServices::isPlainKey( string_view key )
    {
        return key.empty() == false && key.find_first_of( "=\r\n" ) == string_view::npos && StringUtil::trim( key ) == key;
    }

    void LocalPlatformServices::reportUnreadable( const utf8* pFileName ) const
    {
        SW_LOG_ERROR_ID( "LocalPlatformServices.FileUnreadable", "Local platform: '%#' in '%#' exists but could not be read - it is left untouched and changes of this run are not saved", pFileName,
                         _folder.str() );
    }

    bool LocalPlatformServices::writeKeyValueFile( const utf8* pFileName, const flat_map<string, string>& mapValue ) const
    {
        StringBuilder<constant::buffer::kMax1024> text;
        for ( const auto& [key, value] : mapValue )
        {
            text.appendFormat( "%#=%#\n", key, value );
        }
        const AbsolutePath path   = _folder.join( pFileName );
        const bool         bSaved = FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFileDurable( path, text.view() );
        if ( bSaved == false )
            SW_LOG_WARNING_ID( "LocalPlatformServices.FileNotSaved", "Local platform: '%#' could not be written - tried again on the next update", path.c_str() );
        return bSaved;
    }

    bool LocalPlatformServices::readKeyValueFile( const utf8* pFileName, flat_map<string, string>& outMapValue ) const
    {
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( _folder.join( pFileName ), text );
        if ( read != OptionalFileRead::Read )
            return read == OptionalFileRead::Missing; // 없으면 처음 실행이다
        StringUtil::forEachLine( text, [&outMapValue]( string_view line )
        {
            const size_t equals = line.find( '=' );
            if ( equals == string_view::npos || equals == 0 )
                return;
            outMapValue[string( StringUtil::trim( line.substr( 0, equals ) ) )] = string( StringUtil::trim( line.substr( equals + 1 ) ) );
        } );
        return true;
    }
} // namespace sw
