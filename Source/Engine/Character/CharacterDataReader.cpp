#include "pch.h"

#include "Engine/Character/CharacterDataReader.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterData" );
} // namespace sw

namespace sw
{
    CharacterDataReader::CharacterDataReader( string_view sourceName )
        : _sourceName{ sourceName.empty() ? string_view( "<memory>" ) : sourceName }
        , _listError{}
    {
    }

    bool CharacterDataReader::parseRoot( XmlDocument& outDocument, string_view xmlText, const utf8* pRootName, XmlNode& outRoot )
    {
        if ( outDocument.parse( xmlText, _sourceName ) == false )
        {
            addError( outDocument.getLastError() );
            return false;
        }
        outRoot = outDocument.getRoot( pRootName );
        if ( outRoot.isValid() == false )
        {
            addError( string( "root element <" ) + pRootName + "> is missing" );
            return false;
        }
        return true;
    }

    bool CharacterDataReader::loadRoot( XmlDocument& outDocument, string_view path, const utf8* pRootName, XmlNode& outRoot )
    {
        if ( outDocument.loadPath( path ) == false )
        {
            addError( outDocument.getLastError().empty() ? string( "cannot read" ) : outDocument.getLastError() );
            return false;
        }
        outRoot = outDocument.getRoot( pRootName );
        if ( outRoot.isValid() == false )
        {
            addError( string( "root element <" ) + pRootName + "> is missing" );
            return false;
        }
        return true;
    }

    void CharacterDataReader::addError( const XmlNode& node, string_view message )
    {
        string text = _sourceName;
        text += ": <";
        text += node.isValid() ? node.getName() : "?";
        text += "> ";
        text += message;
        _listError.push_back( std::move( text ) );
    }

    void CharacterDataReader::addError( string_view message )
    {
        string text = _sourceName;
        text += ": ";
        text += message;
        _listError.push_back( std::move( text ) );
    }

    void CharacterDataReader::reportUnknownAttributes( const XmlNode& node, const utf8* const* ppKnown, size_t knownCount )
    {
        vector<const utf8*> listUnknown;
        (void)XmlNameCheck::collectUnknownAttributes( node, ppKnown, static_cast<uint32>( knownCount ), listUnknown ); // 수는 목록이 말한다
        for ( const utf8* pName : listUnknown )
        {
            addError( node, string( "has unknown attribute '" ) + pName + "'" );
        }
    }

    void CharacterDataReader::reportUnknownElement( const XmlNode& node )
    {
        addError( node, "is not a known element here" );
    }

    float32 CharacterDataReader::readFloat( const XmlNode& node, const utf8* pName, float32 fallback )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            return fallback;
        float32 value = fallback;
        if ( StringUtil::parseFloat( StringUtil::trim( string_view( pText ) ), value ) == false )
        {
            addError( node, string( "attribute '" ) + pName + "' is not a number: '" + pText + "'" );
            return fallback;
        }
        return value;
    }

    int32 CharacterDataReader::readInt( const XmlNode& node, const utf8* pName, int32 fallback )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            return fallback;
        int32 value = fallback;
        if ( StringUtil::parseInt( StringUtil::trim( string_view( pText ) ), value ) == false )
        {
            addError( node, string( "attribute '" ) + pName + "' is not an integer: '" + pText + "'" );
            return fallback;
        }
        return value;
    }

    bool CharacterDataReader::readBool( const XmlNode& node, const utf8* pName, bool fallback )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            return fallback;
        bool value = fallback;
        if ( StringUtil::tryParseBool( StringUtil::trim( string_view( pText ) ), value ) == false )
        {
            addError( node, string( "attribute '" ) + pName + "' is not a boolean: '" + pText + "'" );
            return fallback;
        }
        return value;
    }

    float3 CharacterDataReader::readFloat3( const XmlNode& node, const utf8* pName, const float3& fallback )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            return fallback;
        vector<string_view> listToken;
        splitTokens( string_view( pText ), listToken );
        float3 value = fallback;
        bool   bOk   = listToken.size() == 3;
        for ( size_t axis = 0; bOk && axis < 3; ++axis )
        {
            bOk = StringUtil::parseFloat( listToken[axis], value.data()[axis] );
        }
        if ( bOk == false )
        {
            addError( node, string( "attribute '" ) + pName + "' is not three numbers: '" + pText + "'" );
            return fallback;
        }
        return value;
    }

    quaternion CharacterDataReader::readRotation( const XmlNode& node, const utf8* pName, const quaternion& fallback )
    {
        if ( hasAttribute( node, pName ) == false )
            return fallback;
        const float3 degrees = readFloat3( node, pName, float3::Zero );
        return quaternion::createFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
    }

    hashed_string CharacterDataReader::readName( const XmlNode& node, const utf8* pName, bool bRequired )
    {
        const string_view text = StringUtil::trim( node.getAttributeText( pName ) );
        if ( text.empty() )
        {
            if ( bRequired )
                addError( node, string( "needs attribute '" ) + pName + "'" );
            return hashed_string{};
        }
        return hashed_string( text );
    }

    void CharacterDataReader::readNameList( const XmlNode& node, const utf8* pName, vector<hashed_string>& outListName )
    {
        outListName.clear();
        vector<string_view> listToken;
        splitTokens( node.getAttributeText( pName ), listToken );
        for ( const string_view token : listToken )
        {
            outListName.push_back( hashed_string( token ) );
        }
    }

    string CharacterDataReader::getErrorText() const
    {
        string text;
        for ( const string& error : _listError )
        {
            if ( text.empty() == false )
                text += "\n";
            text += error;
        }
        return text;
    }

    bool CharacterDataReader::finish() const
    {
        for ( const string& error : _listError )
        {
            SW_LOG_ERROR( "%#", error );
        }
        return _listError.empty();
    }

    void CharacterDataReader::splitTokens( string_view text, vector<string_view>& outListToken )
    {
        outListToken.clear();
        size_t tokenStart = string_view::npos;
        for ( size_t charIndex = 0; charIndex <= text.size(); ++charIndex )
        {
            const bool bEnd       = charIndex == text.size();
            const utf8 character  = bEnd ? ' ' : text[charIndex];
            const bool bSeparator = character == ' ' || character == ',' || character == '\t' || character == '\n' || character == '\r';
            if ( bSeparator == false )
            {
                if ( tokenStart == string_view::npos )
                    tokenStart = charIndex;
                continue;
            }
            if ( tokenStart != string_view::npos )
            {
                outListToken.push_back( text.substr( tokenStart, charIndex - tokenStart ) );
                tokenStart = string_view::npos;
            }
        }
    }

    string CharacterDataReader::formatFloat( float32 value )
    {
        utf8 arrBuffer[constant::kMaxBuffer64]{};
        // -0 을 0 으로 — 쓴 파일이 "-0" 으로 흔들리지 않게.
        const float32 cleanValue = MathUtil::abs( value ) < 1.0e-7f ? 0.0f : value;
        const uint32  length     = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer64, cleanValue );
        return string( arrBuffer, length );
    }

    string CharacterDataReader::formatFloat3( const float3& value )
    {
        return formatFloat( value._x ) + " " + formatFloat( value._y ) + " " + formatFloat( value._z );
    }

    string CharacterDataReader::formatRotation( const quaternion& rotation )
    {
        return formatFloat3( rotation.getEulerAngles() * MathUtil::kRadianToDegree );
    }

    string CharacterDataReader::formatNameList( const vector<hashed_string>& listName )
    {
        string text;
        for ( const hashed_string& name : listName )
        {
            if ( text.empty() == false )
                text += " ";
            text += name.c_str();
        }
        return text;
    }
} // namespace sw
