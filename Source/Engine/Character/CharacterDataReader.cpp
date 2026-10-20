#include "pch.h"

#include "Engine/Character/CharacterDataReader.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

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

    bool CharacterDataReader::hasAttribute( const XMLNode& node, const utf8* pName )
    {
        return node.findAttribute( pName ) != nullptr;
    }

    bool CharacterDataReader::parseRoot( XMLDocument& outDocument, string_view xmlText, const utf8* pRootName, XMLNode& outRoot )
    {
        return reportRootStatus( outDocument, XMLReadUtil::parseRoot( outDocument, xmlText, _sourceName, pRootName, outRoot ), pRootName );
    }

    bool CharacterDataReader::loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot )
    {
        return reportRootStatus( outDocument, XMLReadUtil::loadRoot( outDocument, path, pRootName, outRoot ), pRootName );
    }

    bool CharacterDataReader::reportRootStatus( const XMLDocument& document, XMLRootStatus status, const utf8* pRootName )
    {
        switch ( status )
        {
            case XMLRootStatus::Found:
            {
                return true;
            }
            case XMLRootStatus::NotLoaded:
            {
                addError( document.getLastError().empty() ? string( "cannot read" ) : document.getLastError() );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                addError( string( "root element <" ) + pRootName + "> is missing" );
                return false;
            }
        }
        return false;
    }

    void CharacterDataReader::addError( const XMLNode& node, string_view message )
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

    void CharacterDataReader::reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, size_t knownCount )
    {
        vector<const utf8*> listUnknown;
        (void)XMLNameCheck::collectUnknownAttributes( node, ppKnown, static_cast<uint32>( knownCount ), listUnknown ); // 수는 목록이 말한다
        for ( const utf8* pName : listUnknown )
        {
            addError( node, string( "has unknown attribute '" ) + pName + "'" );
        }
    }

    void CharacterDataReader::reportUnknownElement( const XMLNode& node )
    {
        addError( node, "is not a known element here" );
    }

    const utf8* CharacterDataReader::findRequiredText( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            addError( node, string( "needs attribute '" ) + pName + "'" );
        return pText;
    }

    float32 CharacterDataReader::parseFloatText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        float32 value = 0.0f;
        if ( StringUtil::parseFloat( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( node, string( "attribute '" ) + pName + "' is not a number: '" + pText + "'" );
        return value;
    }

    int32 CharacterDataReader::parseIntText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        int32 value = 0;
        if ( StringUtil::parseInt( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( node, string( "attribute '" ) + pName + "' is not an integer: '" + pText + "'" );
        return value;
    }

    bool CharacterDataReader::parseBoolText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        bool value = false;
        if ( StringUtil::tryParseBool( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( node, string( "attribute '" ) + pName + "' is not a boolean: '" + pText + "'" );
        return value;
    }

    float3 CharacterDataReader::parseFloat3Text( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        float3              value{};
        vector<string_view> listToken;
        splitTokens( string_view( pText ), listToken );
        bool bOk = listToken.size() == 3;
        for ( size_t axis = 0; bOk && axis < 3; ++axis )
        {
            bOk = StringUtil::parseFloat( listToken[axis], value.data()[axis] );
        }
        if ( bOk == false )
            addError( node, string( "attribute '" ) + pName + "' is not three numbers: '" + pText + "'" );
        return value;
    }

    quaternion CharacterDataReader::parseRotationText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        return quaternion::makeFromYawPitchRoll( parseFloat3Text( node, pName, pText ) * MathUtil::kDegreeToRadian );
    }

    float32 CharacterDataReader::readFloat( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = findRequiredText( node, pName );
        return pText != nullptr ? parseFloatText( node, pName, pText ) : 0.0f;
    }

    int32 CharacterDataReader::readInt( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = findRequiredText( node, pName );
        return pText != nullptr ? parseIntText( node, pName, pText ) : 0;
    }

    bool CharacterDataReader::readBool( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = findRequiredText( node, pName );
        return pText != nullptr && parseBoolText( node, pName, pText );
    }

    float3 CharacterDataReader::readFloat3( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = findRequiredText( node, pName );
        return pText != nullptr ? parseFloat3Text( node, pName, pText ) : float3{};
    }

    quaternion CharacterDataReader::readRotation( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = findRequiredText( node, pName );
        return pText != nullptr ? parseRotationText( node, pName, pText ) : quaternion::Identity;
    }

    float32 CharacterDataReader::readOptionalFloat( const XMLNode& node, const utf8* pName, float32 absentValue )
    {
        const utf8* pText = node.findAttribute( pName );
        return pText != nullptr ? parseFloatText( node, pName, pText ) : absentValue;
    }

    int32 CharacterDataReader::readOptionalInt( const XMLNode& node, const utf8* pName, int32 absentValue )
    {
        const utf8* pText = node.findAttribute( pName );
        return pText != nullptr ? parseIntText( node, pName, pText ) : absentValue;
    }

    bool CharacterDataReader::readOptionalBool( const XMLNode& node, const utf8* pName, bool bAbsentValue )
    {
        const utf8* pText = node.findAttribute( pName );
        return pText != nullptr ? parseBoolText( node, pName, pText ) : bAbsentValue;
    }

    float3 CharacterDataReader::readOptionalFloat3( const XMLNode& node, const utf8* pName, const float3& absentValue )
    {
        const utf8* pText = node.findAttribute( pName );
        return pText != nullptr ? parseFloat3Text( node, pName, pText ) : absentValue;
    }

    hashed_string CharacterDataReader::readName( const XMLNode& node, const utf8* pName, bool bRequired )
    {
        const hashed_string name = XMLReadUtil::readName( node, pName );
        if ( name.empty() && bRequired )
            addError( node, string( "needs attribute '" ) + pName + "'" );
        return name;
    }

    void CharacterDataReader::readNameList( const XMLNode& node, const utf8* pName, vector<hashed_string>& outListName )
    {
        outListName.clear();
        vector<string_view> listToken;
        splitTokens( node.getAttributeText( pName ), listToken );
        for ( const string_view token : listToken )
        {
            outListName.push_back( hashed_string( token ) );
        }
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
        // -0 을 0 으로 바꾼다. 쓴 파일이 "-0" 으로 흔들리지 않게 하려는 것이다.
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
