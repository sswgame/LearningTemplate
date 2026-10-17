/**
 * @file LogOnce.h
 * @brief "같은 일은 한 번만 알린다" 경고의 상태입니다. 상태는 범위(scope)에 속하고, 범위를 새로 열면 지금까지 알린 키를 잊습니다.
 * @details 함수 안 정적 집합으로 "한 번만"을 지키면 그 상태가 프로세스 끝까지 남습니다. 그러면 같은 테스트가 혼자 돌 때는 경고를 내고 다른 테스트
 *          뒤에 돌 때는 내지 않아, 로그를 세는 판정(`HostLogJudge`)의 결과가 테스트 순서에 따라 바뀝니다. 상태를 여기 한 곳에 두고 테스트 하니스가
 *          케이스마다 `beginScope` 를 불러 범위를 케이스로 맞춥니다. 게임 · 에디터 · 서버는 범위를 열지 않으므로 프로세스가 하나의 범위입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum LogOnceKind
     * @brief "한 번만" 경고의 종류입니다. 종류마다 키 공간이 따로라 서로 다른 종류의 같은 키가 겹치지 않습니다.
     */
    enum class LogOnceKind : uint8
    {
        SkippedField,      ///< 바이너리 엄격 읽기가 타입에 없는 필드를 건너뜀(타입 × 필드 이름 해시)
        UnreadableField,   ///< 바이너리 엄격 읽기가 읽지 못한 필드를 건너뜀(타입 × 필드)
        UnknownEnumerator, ///< 저장된 열거자 이름을 모름(열거형 × 열거자 이름 해시)
        Count
    };

    /**
     * @struct LogOnce
     * @brief 종류와 키마다 범위 안에서 처음인지 답합니다. 여러 스레드가 함께 부릅니다.
     */
    struct SW_API LogOnce
    {
        /** @brief 이번 범위에서 @p kind 의 @p key 를 처음 알리는 것이면 true 이고, 그 키를 알린 것으로 적습니다. */
        [[nodiscard]] static bool tryClaim( LogOnceKind kind, uint64 key );
        /** @brief 새 범위를 엽니다 — 모든 종류의 알린 키를 잊습니다. 테스트 하니스가 케이스마다 부릅니다. */
        static void beginScope();
    };
} // namespace sw
