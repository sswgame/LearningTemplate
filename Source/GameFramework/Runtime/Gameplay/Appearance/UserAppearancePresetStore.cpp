#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"

namespace sw
{
    namespace
    {
        struct UserAppearancePresetStoreInternal
        {
            static constexpr int32 kMaxPresetCount = 1000; ///< 손으로 고친 세이브가 큰 수를 적어도 그 상한만큼만 읽는다

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
        , _pDatabase{ nullptr }
    {
    }

    UserAppearancePreset* UserAppearancePresetStore::findMutablePreset( string_view name )
    {
        return VectorUtil::findIf( _listPreset, [&]( UserAppearancePreset& preset )
        { return string_view( preset._name.c_str(), preset._name.size() ) == name; } );
    }

    const UserAppearancePreset* UserAppearancePresetStore::findPresetByName( string_view name ) const
    {
        return VectorUtil::findIf( _listPreset, [&]( const UserAppearancePreset& preset )
        { return string_view( preset._name.c_str(), preset._name.size() ) == name; } );
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
        const string          cleanName     = UserAppearancePresetStoreInternal::sanitizeName( toName );
        UserAppearancePreset* pPreset       = findMutablePreset( fromName );
        const bool            bCannotRename = pPreset == nullptr || cleanName.empty() || findPresetByName( string_view( cleanName.c_str(), cleanName.size() ) ) != nullptr;
        if ( bCannotRename )
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

    bool UserAppearancePresetStore::setThumbnailSlot( string_view name, string_view thumbnailSlot )
    {
        UserAppearancePreset* pPreset = findMutablePreset( name );
        if ( pPreset == nullptr )
            return false;
        pPreset->_thumbnailSlot = string( thumbnailSlot );
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

    expected<string> UserAppearancePresetStore::saveToText() const
    {
        UserAppearancePresetFile file;
        file._formatVersion = kVersion;
        file._listPreset.reserve( _listPreset.size() );
        for ( const UserAppearancePreset& preset : _listPreset )
        {
            UserAppearancePresetRecord record;
            record._name          = preset._name;
            record._code          = _pDatabase != nullptr ? AppearanceSelection::encode( preset._selection, *_pDatabase ) : string{};
            record._thumbnailSlot = preset._thumbnailSlot;
            record._bFavorite     = preset._bFavorite == SW_TRUE;
            file._listPreset.push_back( std::move( record ) );
        }
        return JSONSerializer::serializePretty( &file, *UserAppearancePresetFile::StaticType() );
    }

    bool UserAppearancePresetStore::loadFromXMLText( string_view text )
    {
        using Internal = UserAppearancePresetStoreInternal;
        UserAppearancePresetFile file;
        // 모르는 키 · 빠진 필수 칸 · 읽지 못한 값은 키 이름과 함께 오류로 남는다(옛 `key=value` 세이브도 여기서 진다).
        if ( ConfigManager::readConfigJSON( file, text, "appearance presets" ) == false )
            return false;
        if ( file._formatVersion != kVersion )
        {
            SW_LOG_WARNING_ID( "UserAppearancePresetStore.AppearancePresetSaveFormat", "Appearance preset save has format version %# (this build reads %#)", file._formatVersion, kVersion );
            return false;
        }
        _listPreset.clear();
        for ( size_t index = 0; index < file._listPreset.size() && index < static_cast<size_t>( Internal::kMaxPresetCount ); ++index )
        {
            const UserAppearancePresetRecord& record = file._listPreset[index];
            UserAppearancePreset              preset;
            preset._name          = record._name;
            preset._thumbnailSlot = record._thumbnailSlot;
            preset._bFavorite     = record._bFavorite ? SW_TRUE : SW_FALSE;
            if ( _pDatabase == nullptr )
            {
                SW_LOG_WARNING_ID( "UserAppearancePresetStore.AppearancePresetCannotRead", "Appearance preset '%#' cannot be read (%#) - skipped", preset._name, "no appearance data" );
                continue;
            }
            if ( const expected<void> decoded = AppearanceSelection::decode( string_view( record._code ), *_pDatabase, preset._selection ); decoded.has_value() == false )
            {
                SW_LOG_WARNING_ID( "UserAppearancePresetStore.AppearancePresetCannotRead", "Appearance preset '%#' cannot be read (%#) - skipped", preset._name, decoded.error().message() );
                continue;
            }
            _listPreset.push_back( preset );
        }
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
