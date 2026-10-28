/**
 * @file Engine/Foundation/StateHashBuilder.h
 * @brief 씬 상태 해시에 값을 접는 도구입니다 — 컴포넌트가 `Component::foldStateHash` 에서 자기 시뮬레이션 상태를 넣습니다.
 * @details 실수는 비트로 접되 -0.0 은 +0.0 으로, NaN 은 한 비트 패턴으로 바꾼다 — 같은 값이 프로세스마다 다른 비트로 나와(부호 있는 0 · NaN 페이로드)
 *          해시만 갈리는 일을 막는다. 접는 함수는 형식마다 하나라 호출하는 쪽이 비트 변환을 직접 쓰지 않는다(FNV-1a 64).
 */
#pragma once
#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include <cmath>

namespace sw
{
    /** @brief 상태 해시 하나를 쌓습니다. 값을 넣는 순서가 해시의 일부입니다. */
    class StateHashBuilder
    {
    public:
        static constexpr uint32 kCanonicalNanBits = 0x7FC00000u;

        void addBytes( const void* pData, size_t byteCount ) { _hash = HashUtil::appendBytes64( _hash, pData, byteCount ); }
        void addString( string_view text ) { addBytes( text.data(), text.size() ); }
        void addUint8( uint8 value ) { addBytes( &value, sizeof( value ) ); }
        void addUint32( uint32 value ) { addBytes( &value, sizeof( value ) ); }
        void addFloat( float32 value )
        {
            uint32 bits = BitUtil::floatToBits( value );
            if ( std::isnan( value ) )
                bits = kCanonicalNanBits;
            else if ( value == 0.0f )
                bits = 0u; // -0.0 과 +0.0 은 같은 값이다
            addUint32( bits );
        }
        void addFloat2( const float2& value )
        {
            addFloat( value._x );
            addFloat( value._y );
        }
        void addFloat3( const float3& value )
        {
            addFloat( value._x );
            addFloat( value._y );
            addFloat( value._z );
        }
        uint64 getHash() const { return _hash; }

    private:
        uint64 _hash{ HashUtil::kFNVOffset64 };
    };
} // namespace sw
