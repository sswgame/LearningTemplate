#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

namespace sw
{
    namespace
    {
        struct UserAppearancePresetStoreInternal
        {
            /** @brief 플레이어가 적은 이름입니다. 줄바꿈은 이름 표시 한 줄을 깨뜨리므로 공백으로 바꿉니다. */
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserAppearancePresetStore::UserAppearancePresetStore()
        : _listPreset{}
    {
    }

    UserAppearancePresetRecord* UserAppearancePresetStore::findMutablePreset( string_view name )
    {
        return VectorUtil::findIf( _listPreset, [&]( UserAppearancePresetRecord& preset )
        { return string_view( preset._name.c_str(), preset._name.size() ) == name; } );
    }

    const UserAppearancePresetRecord* UserAppearancePresetStore::findPresetByName( string_view name ) const
    {
        return VectorUtil::findIf( _listPreset, [&]( const UserAppearancePresetRecord& preset )
        { return string_view( preset._name.c_str(), preset._name.size() ) == name; } );
    }

    bool UserAppearancePresetStore::savePreset( string_view name, const AppearanceSelection& selection, const AppearanceDatabase& database )
    {
        const string cleanName = UserAppearancePresetStoreInternal::sanitizeName( name );
        if ( cleanName.empty() )
            return false;
        UserAppearancePresetRecord* pPreset = findMutablePreset( string_view( cleanName.c_str(), cleanName.size() ) );
        if ( pPreset == nullptr )
        {
            _listPreset.push_back( UserAppearancePresetRecord{} );
            pPreset        = &_listPreset.back();
            pPreset->_name = cleanName;
        }
        pPreset->_code = AppearanceSelection::encode( selection, database );
        return true;
    }

    expected<void> UserAppearancePresetStore::decodePreset( string_view name, const AppearanceDatabase& database, AppearanceSelection& outSelection ) const
    {
        const UserAppearancePresetRecord* pPreset = findPresetByName( name );
        if ( pPreset == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "no preset with that name" } );
        return AppearanceSelection::decode( string_view( pPreset->_code ), database, outSelection );
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
        const string                cleanName     = UserAppearancePresetStoreInternal::sanitizeName( toName );
        UserAppearancePresetRecord* pPreset       = findMutablePreset( fromName );
        const bool                  bCannotRename = pPreset == nullptr || cleanName.empty() || findPresetByName( string_view( cleanName.c_str(), cleanName.size() ) ) != nullptr;
        if ( bCannotRename )
            return false;
        pPreset->_name = cleanName;
        return true;
    }

    bool UserAppearancePresetStore::setFavorite( string_view name, bool bFavorite )
    {
        UserAppearancePresetRecord* pPreset = findMutablePreset( name );
        if ( pPreset == nullptr )
            return false;
        pPreset->_bFavorite = bFavorite;
        return true;
    }

    bool UserAppearancePresetStore::setThumbnailSlot( string_view name, string_view thumbnailSlot )
    {
        UserAppearancePresetRecord* pPreset = findMutablePreset( name );
        if ( pPreset == nullptr )
            return false;
        pPreset->_thumbnailSlot = string( thumbnailSlot );
        return true;
    }

    void UserAppearancePresetStore::collectFavorites( vector<const UserAppearancePresetRecord*>& outListPreset ) const
    {
        outListPreset.clear();
        for ( const UserAppearancePresetRecord& preset : _listPreset )
        {
            if ( preset._bFavorite )
                outListPreset.push_back( &preset );
        }
    }

    expected<string> UserAppearancePresetStore::saveToText() const
    {
        UserAppearancePresetFile file;
        file._formatVersion = static_cast<int32>( kFormat._version );
        file._listPreset    = _listPreset;
        return JSONSerializer::serializePretty( &file, *UserAppearancePresetFile::StaticType() );
    }

    bool UserAppearancePresetStore::loadFromXMLText( string_view text )
    {
        UserAppearancePresetFile file;
        // 모르는 키 · 빠진 필수 칸 · 읽지 못한 값은 키 이름과 함께 오류로 남는다.
        if ( ConfigManager::readConfigJSON( file, text, "appearance presets" ) == false )
            return false;
        if ( kFormat.accept( static_cast<uint32>( file._formatVersion ), "appearance presets" ) == false )
            return false;
        _listPreset = std::move( file._listPreset );
        return true;
    }

    bool UserAppearancePresetStore::writeBytes( vector<uint8>& outBytes ) const
    {
        const expected<string> text = saveToText();
        if ( text.has_value() == false )
        {
            // 프리셋 이름은 사용자가 친 글이다 - 잘못된 UTF-8 이면 세이브를 빈 글로 덮지 않고 실패한다.
            SW_LOG_ERROR_ID( "UserAppearancePresetStore.NotWritable", "Appearance presets could not be written: %# (%#)", text.error().message(), text.error().detail() );
            return false;
        }
        StringUtil::assignBytes( outBytes, *text );
        return true;
    }

    bool UserAppearancePresetStore::readBytes( const uint8* pData, size_t size )
    {
        return loadFromXMLText( string_view( reinterpret_cast<const utf8*>( pData ), size ) );
    }
} // namespace sw
