/**
 * @file FourCcUtil.h
 * @brief 네 글자 표식(FourCC)을 만드는 도우미입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct FourCcUtil
     * @brief 형식 표식 · 상태 구간 태그를 네 글자로 적게 합니다 — 16 진 값을 손으로 적지 않습니다.
     * @details 바이트 순서는 하나다: 리틀 엔디언으로 쓰면 파일 앞 네 바이트가 글자 순서 그대로다(`make( "SWHF" )` → 파일에 `S W H F`).
     *          DDS · glTF 같은 바깥 형식의 표식과 같은 규약이다. 글자 수가 넷이 아니면 컴파일되지 않는다(`char[5]` 만 받는다).
     */
    struct FourCcUtil
    {
        /** @brief 네 글자로 표식을 만듭니다. */
        [[nodiscard]] static constexpr uint32 make( const utf8 ( &text )[5] ) noexcept
        {
            return static_cast<uint32>( static_cast<uint8>( text[0] ) ) | ( static_cast<uint32>( static_cast<uint8>( text[1] ) ) << 8 ) |
                   ( static_cast<uint32>( static_cast<uint8>( text[2] ) ) << 16 ) | ( static_cast<uint32>( static_cast<uint8>( text[3] ) ) << 24 );
        }
    };

    static_assert( FourCcUtil::make( "DDS " ) == 0x20534444u, "FourCcUtil 는 파일 바이트 순서(리틀 엔디언)다" );
} // namespace sw
