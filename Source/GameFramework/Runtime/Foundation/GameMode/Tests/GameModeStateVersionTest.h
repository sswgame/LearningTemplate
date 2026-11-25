/**
 * @file GameModeStateVersionTest.h
 * @brief 디렉터의 리플렉션 상태 칸을 뺀 판 올림(`SW_TYPE_VERSION_PATCH`)을 디렉터 테스트가 같은 방법으로 확인하는 도우미입니다(Tests 폴더 — 배포에 없다).
 * @details 지금 판 바이트의 머리(타입 판 `uint32` + 칸 배치 해시 `uint64`) 뒤, 남은 칸 @p keptFieldBytes 다음에 뺀 칸의 옛 바이트를 끼워 판 1 바이트를 만든다.
 *          그것을 복원한 디렉터가 다시 쓴 바이트가 처음 바이트와 같으면 패치가 뺀 칸만 버리고 나머지(남은 칸 · 손 본문)를 그대로 옮긴 것이다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include <cstring>

namespace sw::test
{
    struct GameModeStateVersionTest
    {
        /** @brief 리플렉션 상태 본문의 머리 길이입니다(타입 판 + 칸 배치 해시 — `StateIO::reflectedObject`). */
        static constexpr size_t kHeadBytes = sizeof( uint32 ) + sizeof( uint64 );

        /** @brief 지금 판(2) 바이트 @p current 로 판 1 바이트를 만듭니다 — 머리의 판을 1 로 적고 @p removedFieldBytes 를 남은 칸 뒤에 끼운다. */
        static vector<uint8> makeVersion1( const vector<uint8>& current, size_t keptFieldBytes, span<const uint8> removedFieldBytes )
        {
            const size_t  insertAt = kHeadBytes + keptFieldBytes;
            vector<uint8> old( current.begin(), current.begin() + static_cast<std::ptrdiff_t>( insertAt ) );
            const uint32  version1 = 1;
            std::memcpy( old.data(), &version1, sizeof( version1 ) );
            old.insert( old.end(), removedFieldBytes.begin(), removedFieldBytes.end() );
            old.insert( old.end(), current.begin() + static_cast<std::ptrdiff_t>( insertAt ), current.end() );
            return old;
        }

        /**
         * @brief 판이 열린 @p gameMode 의 지금 상태를 판 1 바이트로 바꿔 복원하고, 다시 쓴 바이트가 처음과 같은지 봅니다.
         * @param keptFieldBytes 판 2 에 남은 상태 칸의 바이트 수(뺀 칸 앞)
         * @param removedFieldBytes 뺀 칸의 판 1 바이트
         */
        template <typename TGameMode>
        static void expectVersion1ReadsTheSame( TGameMode& gameMode, size_t keptFieldBytes, span<const uint8> removedFieldBytes )
        {
            Archive saved;
            gameMode.writeState( saved );
            vector<uint8> current;
            saved.writeData( current );
            SW_REQUIRE_TRUE( current.size() >= kHeadBytes + keptFieldBytes );
            uint32 version = 0;
            std::memcpy( &version, current.data(), sizeof( version ) );
            SW_REQUIRE_EQUAL( 2u, version ); // 머리의 첫 칸이 타입 판이다 — 아니면 이 도우미의 가정이 틀렸다

            gameMode.restoreState( makeVersion1( current, keptFieldBytes, removedFieldBytes ) );
            Archive again;
            gameMode.writeState( again );
            vector<uint8> reread;
            again.writeData( reread );
            SW_REQUIRE_EQUAL( current.size(), reread.size() );
            SW_EXPECT_TRUE( std::memcmp( current.data(), reread.data(), current.size() ) == 0 );
        }
    };
} // namespace sw::test
