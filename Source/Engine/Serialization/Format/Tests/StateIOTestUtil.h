/**
 * @file StateIOTestUtil.h
 * @brief 상태 바이트를 고쳐 읽기 거절을 보는 테스트가 함께 쓰는 도우미입니다 — 칸 오프셋을 상수로 적지 않고 `writeState` 가 쓴 바이트의 길이에서 셉니다.
 * @details 리플렉션 상태 칸(`StateIO::reflected`)은 머리(타입 판 + 칸 배치 해시)가 앞에 붙고 칸이 바뀌면 길이가 바뀐다. 손으로 센 오프셋 상수는 그때 조용히 다른 칸을
 *          고쳐 다른 까닭으로 거절되며 거짓 통과한다. 그래서 깨진 바이트는 늘 `writeState` 로 쓴 바이트 위에 값을 덮어써 만들고, 자리는 리플렉션 본문의 길이에서 센다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <cstring>

namespace sw
{
    /**
     * @struct StateIOTestUtil
     * @brief 상태 바이트를 쓰고, 리플렉션 본문의 길이를 재고, 한 자리를 덮어씁니다.
     */
    struct StateIOTestUtil
    {
        /** @brief @p state 의 `writeState` 바이트입니다. */
        template <typename TState>
        [[nodiscard]] static vector<uint8> computeStateBytes( const TState& state )
        {
            Archive archive;
            state.writeState( archive );
            return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
        }

        /** @brief @p state 의 리플렉션 본문(`StateIO::reflected` — 머리 + 상태 칸)의 바이트 수입니다. 손 본문의 첫 칸은 형식 머리 뒤 이 길이에 있습니다. */
        template <typename TState>
        [[nodiscard]] static size_t computeReflectedBytes( const TState& state )
        {
            Archive archive;
            StateIO io( archive );
            io.reflected( const_cast<TState&>( state ) ); // 쓰기 방향이라 고치지 않는다
            return static_cast<size_t>( archive.getSize() );
        }

        /** @brief @p inoutBytes 의 @p offset 자리에 @p value 의 바이트(아카이브와 같은 리틀 엔디언 그대로)를 덮어씁니다. 자리가 바이트 밖이면 false 이고 그대로입니다. */
        template <typename TValue>
        [[nodiscard]] static bool overwrite( vector<uint8>& inoutBytes, const size_t offset, const TValue& value )
        {
            if ( offset + sizeof( TValue ) > inoutBytes.size() )
                return false;
            std::memcpy( inoutBytes.data() + offset, &value, sizeof( TValue ) );
            return true;
        }
    };
} // namespace sw
