#include "pch.h"

#include "Engine/Serialization/Serializer/SecretValue.h"

#include "Core/Hash/SecureUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    void SecretValueUtil::registerSerializeHandler( SerializeContext& context )
    {
        for ( const utf8* pTypeName : { "Secret32", "SecretText" } )
        {
            context.registerTextHandler(
                hashed_string( pTypeName ),
                []( const void* )
            { return string( kRedactedText ); },
                []( void*, string_view )
            { return false; } ); // 글에는 값이 없다 — 가린 글을 값으로 읽지 않는다
        }
    }

    Secret32::~Secret32()
    {
        wipe();
    }

    bool Secret32::isEqual( const Secret32& other ) const
    {
        return SecureUtil::equalsConstantTime( _arrByte, other._arrByte, sizeof( _arrByte ) );
    }

    void Secret32::wipe()
    {
        Memory::wipe( _arrByte, sizeof( _arrByte ) );
    }

    SecretText::SecretText( string_view text )
        : _text{ text }
    {
    }

    SecretText::SecretText( const string& text )
        : _text{ text }
    {
    }

    SecretText::SecretText( const utf8* pText )
        : _text{ pText }
    {
    }

    SecretText::~SecretText()
    {
        wipe();
    }

    SecretText& SecretText::operator=( const SecretText& other )
    {
        if ( this != &other )
        {
            wipe(); // 짧은 글로 덮으면 앞 글의 꼬리가 버퍼에 남는다
            _text = other._text;
        }
        return *this;
    }

    void SecretText::wipe()
    {
        StringUtil::wipeSecret( _text );
    }
} // namespace sw
