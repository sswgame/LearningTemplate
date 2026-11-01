#include "pch.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    bool EditorIconGlyphTable::parseCodepoint( string_view text, uint16& outCodepoint )
    {
        constexpr string_view kPrefix = "U+";
        if ( StringUtil::startsWith( text, kPrefix ) == false || text.size() != kPrefix.size() + 4 )
            return false;
        uint32 value{ 0 };
        for ( const utf8 ch : text.substr( kPrefix.size() ) )
        {
            const bool bDigit = ch >= '0' && ch <= '9';
            const bool bUpper = ch >= 'A' && ch <= 'F';
            if ( bDigit == false && bUpper == false )
                return false;
            value = value * 16 + static_cast<uint32>( bDigit ? ch - '0' : ch - 'A' + 10 );
        }
        if ( value == 0 )
            return false; // ImGui 범위 표는 0 으로 끝난다
        outCodepoint = static_cast<uint16>( value );
        return true;
    }

    bool EditorIconGlyphTable::loadFromAsset( const AssetID& assetID )
    {
        *this = EditorIconGlyphTable{};
        if ( XMLSerializer::loadFile( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "EditorIcon.TableCannotRead", "Editor icon table could not be read strictly: %#", assetID.view() );
            return false;
        }

        bool           bValid = true;
        vector<uint16> listCodepoint;
        for ( const EditorIconGlyph& icon : _listIcon )
        {
            uint16 codepoint{ 0 };
            if ( icon._name.empty() || parseCodepoint( icon._codepoint, codepoint ) == false )
            {
                SW_LOG_ERROR_ID( "EditorIcon.BadRow", "Editor icon '%#' has an empty name or a codepoint that is not U+XXXX: '%#'", icon._name.c_str(), icon._codepoint.c_str() );
                bValid = false;
                continue;
            }
            if ( VectorUtil::contains( listCodepoint, codepoint ) || VectorUtil::findBy<&EditorIconGlyph::_name>( _listIcon, icon._name ) != &icon )
            {
                SW_LOG_ERROR_ID( "EditorIcon.Duplicate", "Editor icon '%#' (%#) repeats a name or a codepoint", icon._name.c_str(), icon._codepoint.c_str() );
                bValid = false;
            }
            listCodepoint.push_back( codepoint );
            string glyph;
            StringUtil::appendUTF8( glyph, codepoint );
            _listGlyphUTF8.push_back( std::move( glyph ) );
            _listNameKey.emplace_back( string_view{ icon._name } );
        }
        for ( const string& text : _listExtraCodepoint )
        {
            uint16 codepoint{ 0 };
            if ( parseCodepoint( text, codepoint ) == false )
            {
                SW_LOG_ERROR_ID( "EditorIcon.BadRow", "Editor icon table extra codepoint is not U+XXXX: '%#'", text.c_str() );
                bValid = false;
                continue;
            }
            listCodepoint.push_back( codepoint );
        }
        if ( VectorUtil::findBy<&EditorIconGlyph::_name>( _listIcon, string_view{ kMissingIconName } ) == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorIcon.NoMissingIcon", "Editor icon table has no '%#' icon", kMissingIconName );
            bValid = false;
        }
        if ( bValid == false )
        {
            *this = EditorIconGlyphTable{};
            return false;
        }

        // 이어지는 코드포인트는 한 구간으로 묶는다(ImGui 범위 표 — 짝마다 처음 · 끝, 0 으로 끝남).
        std::sort( listCodepoint.begin(), listCodepoint.end() );
        for ( const uint16 codepoint : listCodepoint )
        {
            if ( _listGlyphRange.empty() == false && _listGlyphRange.back() + 1 == codepoint )
                _listGlyphRange.back() = codepoint;
            else
                _listGlyphRange.insert( _listGlyphRange.end(), { codepoint, codepoint } );
        }
        _listGlyphRange.push_back( 0 );
        return true;
    }

    const utf8* EditorIconGlyphTable::findGlyph( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listNameKey.size(); ++index )
        {
            if ( _listNameKey[index] == name )
                return _listGlyphUTF8[index].c_str();
        }
        return nullptr;
    }
} // namespace sw::editor
