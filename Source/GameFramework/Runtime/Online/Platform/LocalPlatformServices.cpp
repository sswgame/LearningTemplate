#include "pch.h"

#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    LocalPlatformServices::LocalPlatformServices( string_view folder )
        : _folder{ folder }
        , _listUnlocked{}
        , _mapStat{}
        , _mapPresence{}
        , _bAchievementsDirty{ SW_FALSE }
        , _bStatsDirty{ SW_FALSE }
        , _bPresenceDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
        string text;
        if ( FileUtil::readOptionalTextFile( FileUtil::joinPath( _folder, kAchievementsFileName ), text ) == OptionalFileRead::Read )
        {
            StringUtil::forEachLine( text, [this]( string_view line )
            {
                const string_view achievementID = StringUtil::trim( line );
                if ( achievementID.empty() == false && VectorUtil::contains( _listUnlocked, string( achievementID ) ) == false )
                    _listUnlocked.push_back( string( achievementID ) );
            } );
        }
        readKeyValueFile( kStatsFileName, _mapStat );
    }

    bool LocalPlatformServices::unlockAchievement( string_view achievementID )
    {
        if ( achievementID.empty() )
            return false;
        if ( VectorUtil::contains( _listUnlocked, string( achievementID ) ) )
            return true; // 이미 풀렸어도 true 다(계약)
        _listUnlocked.push_back( string( achievementID ) );
        _bAchievementsDirty = SW_TRUE;
        return true;
    }

    bool LocalPlatformServices::setStat( string_view statID, int64 value )
    {
        if ( statID.empty() )
            return false;
        _mapStat[string( statID )] = formatToString( "%#", value );
        _bStatsDirty               = SW_TRUE;
        return true;
    }

    bool LocalPlatformServices::writeCloudFile( string_view fileName, const vector<uint8>& bytes )
    {
        if ( isPlainFileName( fileName ) == false )
            return false;
        const string path = FileUtil::joinPath( FileUtil::joinPath( _folder, kCloudFolderName ), fileName );
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool LocalPlatformServices::readCloudFile( string_view fileName, vector<uint8>& outBytes )
    {
        outBytes.clear();
        if ( isPlainFileName( fileName ) == false )
            return false;
        const string path = FileUtil::joinPath( FileUtil::joinPath( _folder, kCloudFolderName ), fileName );
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
        if ( key.empty() )
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
        if ( _bAchievementsDirty == SW_TRUE )
        {
            string text;
            for ( const string& achievementID : _listUnlocked )
            {
                text += achievementID;
                text += '\n';
            }
            const string path   = FileUtil::joinPath( _folder, kAchievementsFileName );
            const bool   bSaved = FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, text );
            if ( bSaved == false )
                SW_LOG_WARNING_ID( "LocalPlatformServices.AchievementsNotSaved", "Local platform: '%#' could not be written - tried again on the next update", path.c_str() );
            _bAchievementsDirty = bSaved ? SW_FALSE : SW_TRUE;
        }
        if ( _bStatsDirty == SW_TRUE )
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
        const bool bSeparator = fileName.find( '/' ) != string_view::npos || fileName.find( '\\' ) != string_view::npos;
        return fileName.empty() == false && bSeparator == false && fileName.find( ".." ) == string_view::npos;
    }

    bool LocalPlatformServices::writeKeyValueFile( const utf8* pFileName, const map<string, string>& mapValue ) const
    {
        string text;
        for ( const auto& [key, value] : mapValue )
        {
            text += key;
            text += '=';
            text += value;
            text += '\n';
        }
        const string path   = FileUtil::joinPath( _folder, pFileName );
        const bool   bSaved = FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, text );
        if ( bSaved == false )
            SW_LOG_WARNING_ID( "LocalPlatformServices.FileNotSaved", "Local platform: '%#' could not be written - tried again on the next update", path.c_str() );
        return bSaved;
    }

    void LocalPlatformServices::readKeyValueFile( const utf8* pFileName, map<string, string>& outMapValue ) const
    {
        string text;
        if ( FileUtil::readOptionalTextFile( FileUtil::joinPath( _folder, pFileName ), text ) != OptionalFileRead::Read )
            return; // 처음 실행이다
        StringUtil::forEachLine( text, [&outMapValue]( string_view line )
        {
            const size_t equals = line.find( '=' );
            if ( equals == string_view::npos || equals == 0 )
                return;
            outMapValue[string( StringUtil::trim( line.substr( 0, equals ) ) )] = string( StringUtil::trim( line.substr( equals + 1 ) ) );
        } );
    }
} // namespace sw
