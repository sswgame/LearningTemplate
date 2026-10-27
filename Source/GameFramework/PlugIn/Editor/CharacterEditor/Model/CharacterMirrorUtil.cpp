#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterMirrorUtilInternal
        {
            static bool isSeparator( utf8 character ) { return character == '_' || character == '.' || character == '-' || character == ' '; }
            static bool isUpper( utf8 character ) { return 'A' <= character && character <= 'Z'; }
            static bool isLower( utf8 character ) { return 'a' <= character && character <= 'z'; }
            static bool isDigit( utf8 character ) { return '0' <= character && character <= '9'; }

            /** @brief ?좏겙???대쫫 ?덉뿉???⑥뼱 寃쎄퀎???볦??붿? 遊낅땲??遺꾨━ 臾몄옄 ?먮뒗 CamelCase 寃쎄퀎). */
            static bool isTokenBoundary( string_view name, size_t position, string_view token )
            {
                const size_t end = position + token.size();

                const bool bLeftOK = position == 0 || isSeparator( token.front() ) || isSeparator( name[position - 1] ) ||
                                     ( isUpper( token.front() ) && ( isLower( name[position - 1] ) || isDigit( name[position - 1] ) ) );
                const bool bRightOK = end == name.size() || isSeparator( token.back() ) || isSeparator( name[end] ) || isUpper( name[end] ) || isDigit( name[end] );
                return bLeftOK && bRightOK;
            }

            /** @brief @p token ??寃쎄퀎??留욊쾶 ?볦씤 泥??먮━瑜?@p token ??吏앹쑝濡?諛붽퓭 @p outMirrorName ???댁뒿?덈떎. */
            static bool replaceToken( string_view name, string_view token, string_view replacement, string& outMirrorName )
            {
                size_t position = name.find( token );
                while ( position != string_view::npos )
                {
                    if ( isTokenBoundary( name, position, token ) )
                    {
                        string mirrored( name.substr( 0, position ) );
                        mirrored.append( replacement.data(), replacement.size() );
                        const string_view tail = name.substr( position + token.size() );
                        mirrored.append( tail.data(), tail.size() );
                        outMirrorName = std::move( mirrored );
                        return true;
                    }
                    position = name.find( token, position + 1 );
                }
                return false;
            }

            static constexpr const utf8* kArrPairAttribute[] = { "first", "second" };

            /** @brief `<Pair>` ?먯냼留덈떎 ?띿쓣 ?쎌뼱 @p inoutRules ???뷀빀?덈떎. ?섎せ???띿? ?ㅻ쪟濡??④퉩?덈떎. */
            static void readRoot( const XMLNode& root, CharacterDataReader& reader, MirrorNameRules& inoutRules )
            {
                reader.reportUnexpectedAttributes( root );
                for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Pair" ) == false )
                    {
                        reader.reportUnknownElement( child );
                        continue;
                    }
                    reader.reportUnknownAttributes( child, kArrPairAttribute );
                    const string_view first  = child.getAttributeText( "first" );
                    const string_view second = child.getAttributeText( "second" );
                    if ( inoutRules.addPair( first, second ) == false )
                        reader.addError( "Character.MirrorPairInvalid", child, "needs two different non-empty attributes 'first' and 'second'" );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool MirrorNameRules::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        MirrorNameRules     loaded;
        if ( reader.parseRoot( document, xmlText, "MirrorNames", root ) )
            CharacterMirrorUtilInternal::readRoot( root, reader, loaded );
        if ( reader.finish() == false )
            return false;
        *this = std::move( loaded );
        return true;
    }

    bool MirrorNameRules::loadFromResource( const ResourceID& resourceID )
    {
        const string_view   path = resourceID.view();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        MirrorNameRules     loaded;
        if ( reader.loadRoot( document, ResourceID{ path }, "MirrorNames", root ) )
            CharacterMirrorUtilInternal::readRoot( root, reader, loaded );
        if ( reader.finish() == false )
            return false;
        *this = std::move( loaded );
        return true;
    }

    string MirrorNameRules::saveToXMLText() const
    {
        XMLDocument document;
        XMLNode     root = document.appendRoot( "MirrorNames" );
        for ( const MirrorTokenPair& pair : _listPair )
        {
            XMLNode node = root.appendChild( "Pair" );
            node.appendAttribute( "first", string_view( pair._first ) );
            node.appendAttribute( "second", string_view( pair._second ) );
        }
        return document.saveToString();
    }

    bool MirrorNameRules::addPair( string_view first, string_view second )
    {
        const bool bEitherEmpty = first.empty() || second.empty();
        if ( bEitherEmpty || first == second )
            return false;
        _listPair.push_back( MirrorTokenPair{ string( first ), string( second ) } );
        return true;
    }

    bool MirrorNameRules::findMirrorName( string_view name, string& outMirrorName ) const
    {
        for ( const MirrorTokenPair& pair : _listPair )
        {
            if ( CharacterMirrorUtilInternal::replaceToken( name, pair._first, pair._second, outMirrorName ) )
                return true;
            if ( CharacterMirrorUtilInternal::replaceToken( name, pair._second, pair._first, outMirrorName ) )
                return true;
        }
        return false;
    }

    bool MirrorNameRules::isCenterName( string_view name ) const
    {
        string unused;
        return findMirrorName( name, unused ) == false;
    }
} // namespace sw::editor

namespace sw::editor
{
    float3 CharacterMirrorUtil::mirrorTranslation( const float3& translation, MirrorAxis axis )
    {
        float3 mirrored = translation;
        switch ( axis )
        {
            case MirrorAxis::X:
            {
                mirrored._x = -mirrored._x;
                break;
            }
            case MirrorAxis::Y:
            {
                mirrored._y = -mirrored._y;
                break;
            }
            case MirrorAxis::Z:
            {
                mirrored._z = -mirrored._z;
                break;
            }
        }
        return mirrored;
    }

    quaternion CharacterMirrorUtil::mirrorRotation( const quaternion& rotation, MirrorAxis axis )
    {
        switch ( axis )
        {
            case MirrorAxis::X:
                return quaternion( rotation._x, -rotation._y, -rotation._z, rotation._w );
            case MirrorAxis::Y:
                return quaternion( -rotation._x, rotation._y, -rotation._z, rotation._w );
            case MirrorAxis::Z:
                return quaternion( -rotation._x, -rotation._y, rotation._z, rotation._w );
        }
        return rotation;
    }
} // namespace sw::editor
