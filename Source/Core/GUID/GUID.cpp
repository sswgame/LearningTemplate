#include "pch.h"

#include "Core/GUID/GUID.h"

#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Math/SecureRandom.h"

namespace sw
{
    namespace
    {
        struct GUIDInternal
        {
            static constexpr uint32 kHalfLength = static_cast<uint32>( GUID::kTextLength / 2 );

            static bool isLowerHex( utf8 ch ) { return ( '0' <= ch && ch <= '9' ) || ( 'a' <= ch && ch <= 'f' ); }
        };

        static_assert( static_cast<size_t>( GUIDInternal::kHalfLength ) * 2 == GUID::kTextLength );
    } // namespace

    GUID GUID::create()
    {
        // 운영체제 난수에서 곧바로 받는다(Windows 는 BCryptGenRandom, 리눅스는 getrandom). 32 비트 씨앗 하나로 시작한 난수열은 다른 실행이 같은 씨앗을 뽑으면
        // 같은 GUID 열을 다시 낸다(씨앗이 2^32 가지뿐이라 편집기를 수만 번 열면 일어난다). 에셋 GUID 는 여러 사람의 작업이 저장소에서 합쳐지므로 겹치면 참조가
        // 엉뚱한 에셋으로 풀린다. 언리얼 FGuid::NewGuid 도 OS 의 GUID 를 쓴다. 모두 0 은 "없음" 이라 다시 뽑는다(2^-128).
        for ( ;; )
        {
            const uint64 high = SecureRandom::nextUint64();
            const uint64 low  = SecureRandom::nextUint64();
            if ( high != 0 || low != 0 )
                return GUID{ high, low };
        }
    }

    optional<GUID> GUID::tryParse( string_view text )
    {
        if ( text.size() != kTextLength )
            return nullopt;
        for ( const utf8 ch : text )
        {
            if ( GUIDInternal::isLowerHex( ch ) == false )
                return nullopt;
        }
        uint64 high = 0;
        uint64 low  = 0;
        if ( HexDigitUtil::tryParseFixed( text.substr( 0, GUIDInternal::kHalfLength ), high ) == false ||
             HexDigitUtil::tryParseFixed( text.substr( GUIDInternal::kHalfLength ), low ) == false )
            return nullopt;
        return GUID{ high, low };
    }

    string GUID::toString() const
    {
        string out( kTextLength, '0' );
        HexDigitUtil::writeDigits( out.data(), _high, GUIDInternal::kHalfLength, HexCase::Lower );
        HexDigitUtil::writeDigits( out.data() + GUIDInternal::kHalfLength, _low, GUIDInternal::kHalfLength, HexCase::Lower );
        return out;
    }
} // namespace sw
