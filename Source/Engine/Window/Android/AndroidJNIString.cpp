#include "pch.h"

#include "Engine/Window/Android/AndroidJNIString.h"

#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    static_assert( sizeof( jchar ) == sizeof( uint16 ), "jchar is a UTF-16 code unit" );

    string AndroidJNIString::toUTF8( JNIEnv* pEnv, jstring text )
    {
        string result;
        if ( pEnv == nullptr || text == nullptr )
            return result;
        const jsize        length = pEnv->GetStringLength( text );
        const jchar* const pUnit  = pEnv->GetStringChars( text, nullptr );
        if ( pUnit == nullptr )
            return result;
        StringUtil::utf16UnitsToUTF8( reinterpret_cast<const uint16*>( pUnit ), static_cast<size_t>( length ), result );
        pEnv->ReleaseStringChars( text, pUnit );
        return result;
    }

    jstring AndroidJNIString::toJava( JNIEnv* pEnv, string_view text )
    {
        if ( pEnv == nullptr )
            return nullptr;
        vector<uint16> listUnit;
        StringUtil::utf8ToUTF16Units( text, listUnit );
        static constexpr jchar kEmptyUnit = 0; // 빈 글에도 널이 아닌 포인터를 준다(CheckJNI 가 널을 거절한다)
        const jchar* const     pUnit      = listUnit.empty() ? &kEmptyUnit : reinterpret_cast<const jchar*>( listUnit.data() );
        return pEnv->NewString( pUnit, static_cast<jsize>( listUnit.size() ) );
    }
} // namespace sw
