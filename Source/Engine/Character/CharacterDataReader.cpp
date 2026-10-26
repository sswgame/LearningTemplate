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
                addError( "Character.FileUnreadable", document.getLastError().empty() ? string( "cannot read" ) : document.getLastError() );
                return false;
            }
            case XMLRootStatus::RootMissing:
            {
                addError( "Character.RootMissing", string( "root element <" ) + pRootName + "> is missing" );
                return false;
            }
        }
        return false;
    }

    void CharacterDataReader::addError( const utf8* pID, const XMLNode& node, string_view message )
    {
        string text = _sourceName;
        text += ": <";
        text += node.isValid() ? node.getName() : "?";
        text += "> ";
        text += message;
        _listError.push_back( { std::move( text ), pID } );
    }

    void CharacterDataReader::addError( const utf8* pID, string_view message )
    {
        string text = _sourceName;
        text += ": ";
        text += message;
        _listError.push_back( { std::move( text ), pID } );
    }

    void CharacterDataReader::addIssues( const CharacterIssueList& issues )
    {
        for ( const CharacterIssue& issue : issues.getIssues() )
        {
            addError( issue._pID, string_view( issue._text.c_str(), issue._text.size() ) );
        }
    }

    void CharacterDataReader::reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, size_t knownCount )
    {
        vector<const utf8*> listUnknown;
        (void)XMLNameCheck::collectUnknownAttributes( node, ppKnown, static_cast<uint32>( knownCount ), listUnknown ); // 수는 목록이 말한다
        for ( const utf8* pName : listUnknown )
        {
            addError( "Character.UnknownAttribute", node, string( "has unknown attribute '" ) + pName + "'" );
        }
    }

    void CharacterDataReader::reportUnknownElement( const XMLNode& node )
    {
        addError( "Character.ElementUnknown", node, "is not a known element here" );
    }

    const utf8* CharacterDataReader::findRequiredText( const XMLNode& node, const utf8* pName )
    {
        const utf8* pText = node.findAttribute( pName );
        if ( pText == nullptr )
            addError( "Character.AttributeMissing", node, string( "needs attribute '" ) + pName + "'" );
        return pText;
    }

    float32 CharacterDataReader::parseFloatText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        float32 value = 0.0f;
        if ( StringUtil::parseFloat( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( "Character.NumberInvalid", node, string( "attribute '" ) + pName + "' is not a number: '" + pText + "'" );
        return value;
    }

    int32 CharacterDataReader::parseIntText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        int32 value = 0;
        if ( StringUtil::parseInt( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( "Character.IntegerInvalid", node, string( "attribute '" ) + pName + "' is not an integer: '" + pText + "'" );
        return value;
    }

    bool CharacterDataReader::parseBoolText( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        bool value = false;
        if ( StringUtil::tryParseBool( StringUtil::trim( string_view( pText ) ), value ) == false )
            addError( "Character.BooleanInvalid", node, string( "attribute '" ) + pName + "' is not a boolean: '" + pText + "'" );
        return value;
    }

    float3 CharacterDataReader::parseFloat3Text( const XMLNode& node, const utf8* pName, const utf8* pText )
    {
        float3 value{};
        if ( StringUtil::parseExactFloats( string_view( pText ), kTokenSeparators, value.data(), 3 ) == false )
            addError( "Character.Vector3Invalid", node, string( "attribute '" ) + pName + "' is not three numbers: '" + pText + "'" );
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

    // 선택 필드: 없으면 넣어 둔 값 그대로, 깨졌으면 오류만 남기고 값은 그대로다(로드는 `finish` 에서 실패한다).
#define SW_CHARACTER_OPTIONAL_READER( FunctionName, Type, ParseFunction )                              \
    void CharacterDataReader::FunctionName( const XMLNode& node, const utf8* pName, Type& inoutValue ) \
    {                                                                                                  \
        const utf8* pText = node.findAttribute( pName );                                               \
        if ( pText == nullptr )                                                                        \
            return;                                                                                    \
        const size_t errorCountBefore = _listError.size();                                             \
        const Type   value            = ParseFunction( node, pName, pText );                           \
        if ( _listError.size() == errorCountBefore )                                                   \
            inoutValue = value;                                                                        \
    }
    SW_CHARACTER_OPTIONAL_READER( readOptionalFloat, float32, parseFloatText )
    SW_CHARACTER_OPTIONAL_READER( readOptionalInt, int32, parseIntText )
    SW_CHARACTER_OPTIONAL_READER( readOptionalBool, bool, parseBoolText )
    SW_CHARACTER_OPTIONAL_READER( readOptionalFloat3, float3, parseFloat3Text )
#undef SW_CHARACTER_OPTIONAL_READER

    hashed_string CharacterDataReader::readName( const XMLNode& node, const utf8* pName, bool bRequired )
    {
        const hashed_string name = XMLReadUtil::readOptionalName( node, pName );
        if ( name.empty() && bRequired )
            addError( "Character.AttributeMissing", node, string( "needs attribute '" ) + pName + "'" );
        return name;
    }

    void CharacterDataReader::readNameList( const XMLNode& node, const utf8* pName, vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const string_view token : StringUtil::split( node.getAttributeText( pName ), kTokenSeparators, { ._bDropEmpty = true } ) )
        {
            const hashed_string name( token );
            // 빈 이름의 철자(`none`)는 이름이 될 수 없다 — 목록에 빈 이름이 조용히 들어가지 않게 오류로 남긴다.
            if ( name.empty() )
            {
                addError( "Character.NameEmpty", node, string( "attribute '" ) + pName + "' lists the empty name (none)" );
                continue;
            }
            outListName.push_back( name );
        }
    }

    bool CharacterDataReader::finish() const
    {
        for ( const Error& error : _listError )
        {
            // id 는 호출한 쪽이 글자 상수로 넘긴 것이다(`addError` 의 약속).
            SW_LOG_WRITE( sw::LogLevel::Error, error._pID, "%#", error._text );
        }
        return _listError.empty();
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
