#include "pch.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    bool EditorIconGlyphTable::parseCodepoint( string_view text, uint16& outCodepoint )
    {
        constexpr uint32 kMaxBasicPlaneCodepoint = 0xFFFFu; // ImGui 글리프 범위가 16 비트다
        uint32           value{ 0 };
        if ( StringUtil::tryParseCodepointNotation( text, value ) == false || value == 0 || value > kMaxBasicPlaneCodepoint )
            return false; // 0 은 ImGui 범위 표의 끝 표시다
        outCodepoint = static_cast<uint16>( value );
        return true;
    }

    bool EditorIconGlyphTable::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            *this = EditorIconGlyphTable{};
            SW_LOG_ERROR_ID( "EditorIcon.TableCannotRead", "Editor icon table could not be read: %#", assetID.view() );
            return false;
        }
        return loadFromXMLText( text, assetID.view() );
    }

    bool EditorIconGlyphTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = EditorIconGlyphTable{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "EditorIcon.TableCannotRead", "Editor icon table could not be read strictly: %#", sourceName );
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

    namespace
    {
        /** @brief 셸이 게시한 표와 이미 알린 이름입니다. 셸은 프로세스에 하나이고 그리기 스레드 하나가 읽는다. */
        struct EditorIconPublished
        {
            const EditorIconGlyphTable*     _pGlyphTable{ nullptr };
            const EditorComponentIconTable* _pComponentTable{ nullptr };
            vector<hashed_string>           _listReported;
        };

        EditorIconPublished& published()
        {
            static EditorIconPublished s_published;
            return s_published;
        }
    } // namespace

    void EditorIconGlyphTable::publish( const EditorIconGlyphTable* pGlyphTable, const EditorComponentIconTable* pComponentTable )
    {
        EditorIconPublished& state = published();
        state._pGlyphTable         = pGlyphTable;
        state._pComponentTable     = pComponentTable;
        state._listReported.clear();
    }

    const EditorComponentIconTable& EditorIconGlyphTable::getComponentIconTable()
    {
        const EditorComponentIconTable* pTable = published()._pComponentTable;
        if ( pTable == nullptr )
            SW_FATAL( "The component icon table is asked for before the editor shell published it" );
        return *pTable;
    }

    const utf8* EditorIconGlyphTable::findPublishedGlyph( const hashed_string& name )
    {
        EditorIconPublished&       state = published();
        static const hashed_string s_missingName{ EditorIconGlyphTable::kMissingIconName };
        if ( state._pGlyphTable != nullptr )
        {
            if ( const utf8* pGlyph = state._pGlyphTable->findGlyph( name ) )
                return pGlyph;
        }
        const utf8* const pMissing = state._pGlyphTable != nullptr ? state._pGlyphTable->findGlyph( s_missingName ) : nullptr;
        if ( VectorUtil::contains( state._listReported, name ) == false )
        {
            state._listReported.push_back( name );
            if ( pMissing == nullptr )
                SW_LOG_ERROR_ID( "EditorIcon.TableNotLoaded", "Editor icon '%#' was asked for before the icon table was published (EditorFontSetup) - a static table must hold the name, not the glyph", name.c_str() );
            else
                SW_LOG_ERROR_ID( "EditorIcon.UnknownName", "Editor icon '%#' is not in the icon table (editor/fonts/sweditoricons.glyphs.xml)", name.c_str() );
        }
        return pMissing != nullptr ? pMissing : "?";
    }

    const utf8* EditorIconGlyphTable::findGlyphByName( const utf8* pName )
    {
        return pName != nullptr && pName[0] != 0 ? findPublishedGlyph( hashed_string{ pName } ) : nullptr;
    }
} // namespace sw::editor
