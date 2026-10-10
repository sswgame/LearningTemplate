#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Utility/KeyValueFile.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

namespace sw
{
    SW_LOG_CALLER( "UserAppearancePresetStore" );

    namespace
    {
        struct UserAppearancePresetStoreInternal
        {
            static constexpr int32 kMaxPresetCount = 1000; ///< 손으로 고친 세이브가 큰 수를 말해도 그만큼만 읽는다

            static string makeKey( int32 index, const utf8* pField )
            {
                string key = "preset";
                key += to_string( index );
                key += ".";
                key += pField;
                return key;
            }

            /** @brief 플레이어가 적은 이름 — 줄바꿈은 한 줄 세이브를 깨므로 공백으로 바꾼다. */
            static string sanitizeName( string_view name )
            {
                string result( name );
                for ( utf8& character : result )
                {
                    if ( character == '\n' || character == '\r' )
                        character = ' ';
                }
                return string( StringUtil::trim( string_view( result.c_str(), result.size() ) ) );
            }

            /**
             * @brief 판 1 → 2: `version` → `formatVersion`, `count` → `presetCount`, 즐겨찾기 목록(`favorites=이름,이름`) → 프리셋마다 `.favorite=1`.
             */
            static void upgradeFromVersion1( KeyValueMap& inoutMap )
            {
                const int32    count = KeyValueFile::getInt( inoutMap, "count", 0 );
                vector<string> listFavorite;
                GameDataXML::forEachToken( string_view( KeyValueFile::get( inoutMap, "favorites", "" ) ), ",", [&]( string_view token )
                {
                    listFavorite.push_back( string( StringUtil::trim( token ) ) );
                } );
                for ( int32 index = 0; index < count && index < kMaxPresetCount; ++index )
                {
                    const string name      = KeyValueFile::get( inoutMap, makeKey( index, "name" ), "" );
                    bool         bFavorite = false;
                    for ( const string& favorite : listFavorite )
                    {
                        bFavorite = bFavorite || favorite == name;
                    }
                    inoutMap[makeKey( index, "favorite" )] = bFavorite ? "1" : "0";
                }
                inoutMap["presetCount"]   = to_string( count );
                inoutMap["formatVersion"] = "2";
                inoutMap.erase( "count" );
                inoutMap.erase( "version" );
                inoutMap.erase( "favorites" );
            }

            /** @brief 파일의 판을 지금 판까지 올립니다. 모르는(새) 판이면 false 입니다. */
            static bool upgradeToCurrent( KeyValueMap& inoutMap )
            {
                int32 version = KeyValueFile::getInt( inoutMap, "formatVersion", 0 );
                if ( version == 0 )
                    version = KeyValueFile::getInt( inoutMap, "version", 0 );
                if ( version <= 0 || version > UserAppearancePresetStore::kVersion )
                    return false;
                if ( version == 1 )
                {
                    upgradeFromVersion1( inoutMap );
                    version = 2;
                }
                return version == UserAppearancePresetStore::kVersion;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserAppearancePresetStore::UserAppearancePresetStore()
        : _listPreset{}
        , _pDatabase{ nullptr }
    {
    }

    UserAppearancePreset* UserAppearancePresetStore::findMutablePreset( string_view name )
    {
        for ( UserAppearancePreset& preset : _listPreset )
        {
            if ( string_view( preset._name.c_str(), preset._name.size() ) == name )
                return &preset;
        }
        return nullptr;
    }

    const UserAppearancePreset* UserAppearancePresetStore::findPresetByName( string_view name ) const
    {
        for ( const UserAppearancePreset& preset : _listPreset )
        {
            if ( string_view( preset._name.c_str(), preset._name.size() ) == name )
                return &preset;
        }
        return nullptr;
    }

    int32 UserAppearancePresetStore::savePreset( string_view name, const AppearanceSelection& selection )
    {
        const string          cleanName = UserAppearancePresetStoreInternal::sanitizeName( name );
        UserAppearancePreset* pExisting = findMutablePreset( string_view( cleanName.c_str(), cleanName.size() ) );
        if ( pExisting != nullptr )
        {
            pExisting->_selection = selection;
            return static_cast<int32>( pExisting - _listPreset.data() );
        }
        UserAppearancePreset preset;
        preset._name      = cleanName;
        preset._selection = selection;
        _listPreset.push_back( preset );
        return static_cast<int32>( _listPreset.size() ) - 1;
    }

    bool UserAppearancePresetStore::removePreset( string_view name )
    {
        for ( size_t index = 0; index < _listPreset.size(); ++index )
        {
            if ( string_view( _listPreset[index]._name.c_str(), _listPreset[index]._name.size() ) == name )
            {
                _listPreset.erase( _listPreset.begin() + static_cast<ptrdiff_t>( index ) );
                return true;
            }
        }
        return false;
    }

    bool UserAppearancePresetStore::renamePreset( string_view fromName, string_view toName )
    {
        const string          cleanName = UserAppearancePresetStoreInternal::sanitizeName( toName );
        UserAppearancePreset* pPreset   = findMutablePreset( fromName );
        if ( pPreset == nullptr || cleanName.empty() || findPresetByName( string_view( cleanName.c_str(), cleanName.size() ) ) != nullptr )
            return false;
        pPreset->_name = cleanName;
        return true;
    }

    bool UserAppearancePresetStore::setFavorite( string_view name, bool bFavorite )
    {
        UserAppearancePreset* pPreset = findMutablePreset( name );
        if ( pPreset == nullptr )
            return false;
        pPreset->_bFavorite = bFavorite ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool UserAppearancePresetStore::setThumbnailPath( string_view name, string_view thumbnailPath )
    {
        UserAppearancePreset* pPreset = findMutablePreset( name );
        if ( pPreset == nullptr )
            return false;
        pPreset->_thumbnailPath = string( thumbnailPath );
        return true;
    }

    void UserAppearancePresetStore::collectFavorites( vector<const UserAppearancePreset*>& outListPreset ) const
    {
        outListPreset.clear();
        for ( const UserAppearancePreset& preset : _listPreset )
        {
            if ( preset._bFavorite == SW_TRUE )
                outListPreset.push_back( &preset );
        }
    }

    string UserAppearancePresetStore::saveToText() const
    {
        using Internal = UserAppearancePresetStoreInternal;
        KeyValueMap map;
        map["formatVersion"] = to_string( kVersion );
        map["presetCount"]   = to_string( static_cast<int32>( _listPreset.size() ) );
        for ( size_t index = 0; index < _listPreset.size(); ++index )
        {
            const UserAppearancePreset& preset          = _listPreset[index];
            const int32                 slot            = static_cast<int32>( index );
            map[Internal::makeKey( slot, "name" )]      = preset._name;
            map[Internal::makeKey( slot, "favorite" )]  = preset._bFavorite == SW_TRUE ? "1" : "0";
            map[Internal::makeKey( slot, "thumbnail" )] = preset._thumbnailPath;
            map[Internal::makeKey( slot, "code" )]      = _pDatabase != nullptr ? AppearanceShareCode::encode( preset._selection, *_pDatabase ) : string{};
        }
        return KeyValueFile::dump( map, "UserAppearancePresetStore" );
    }

    bool UserAppearancePresetStore::loadFromText( string_view text )
    {
        using Internal = UserAppearancePresetStoreInternal;
        KeyValueMap map;
        if ( KeyValueFile::parse( text, map ) == false )
            return false;
        if ( Internal::upgradeToCurrent( map ) == false )
        {
            SW_LOG_WARNING( "Appearance preset save has an unsupported format version" );
            return false;
        }
        _listPreset.clear();
        const int32 count = KeyValueFile::getInt( map, "presetCount", 0 );
        for ( int32 index = 0; index < count && index < Internal::kMaxPresetCount; ++index )
        {
            UserAppearancePreset preset;
            preset._name          = KeyValueFile::get( map, Internal::makeKey( index, "name" ), "" );
            preset._thumbnailPath = KeyValueFile::get( map, Internal::makeKey( index, "thumbnail" ), "" );
            preset._bFavorite     = KeyValueFile::getBool( map, Internal::makeKey( index, "favorite" ), false ) ? SW_TRUE : SW_FALSE;
            const utf8* pCode     = KeyValueFile::get( map, Internal::makeKey( index, "code" ), "" );
            string      reason;
            if ( _pDatabase == nullptr || AppearanceShareCode::decode( string_view( pCode ), *_pDatabase, preset._selection, &reason ) == false )
            {
                SW_LOG_WARNING( "Appearance preset '%#' cannot be read (%#) - skipped", preset._name, _pDatabase == nullptr ? "no appearance data" : reason.c_str() );
                continue;
            }
            _listPreset.push_back( preset );
        }
        return true;
    }

    bool UserAppearancePresetStore::writeBytes( vector<uint8>& outBytes ) const
    {
        const string text = saveToText();
        outBytes.assign( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        return true;
    }

    bool UserAppearancePresetStore::readBytes( const uint8* pData, size_t size )
    {
        return loadFromText( string_view( reinterpret_cast<const utf8*>( pData ), size ) );
    }
} // namespace sw
