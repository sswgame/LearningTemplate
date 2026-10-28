/**
 * @file LogOnce.h
 * @brief "같은 일은 한 번만 알린다" 경고의 상태입니다. 상태는 범위(scope)에 속하고, 범위를 새로 열면 지금까지 알린 키를 잊습니다.
 * @details 함수 안 정적 집합으로 "한 번만"을 지키면 그 상태가 프로세스 끝까지 남습니다. 그러면 같은 테스트가 혼자 돌 때는 경고를 내고 다른 테스트
 *          뒤에 돌 때는 내지 않아, 로그를 세는 판정(`CaseLogJudge`)의 결과가 테스트 순서에 따라 바뀝니다. 상태를 여기 한 곳에 두고 테스트 하니스가
 *          케이스마다 `beginScope` 를 불러 범위를 케이스로 맞춥니다. 게임 · 에디터 · 서버는 범위를 열지 않으므로 프로세스가 하나의 범위입니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"

namespace sw
{
    /**
     * @enum LogOnceKind
     * @brief "한 번만" 경고의 종류입니다. 종류마다 키 공간이 따로라 서로 다른 종류의 같은 키가 겹치지 않습니다.
     */
    enum class LogOnceKind : uint8
    {
        SkippedField,       ///< 바이너리 엄격 읽기가 타입에 없는 필드를 건너뜀(타입 × 필드 이름 해시)
        UnreadableField,    ///< 바이너리 엄격 읽기가 읽지 못한 필드를 건너뜀(타입 × 필드)
        UnknownEnumerator,  ///< 저장된 열거자 이름을 모름(열거형 × 열거자 이름 해시)
        BusMessageRejected, ///< 서버 버스가 받은 메시지를 읽지 못해 버림(타입 × 결과 × 받은 버전)
        PushRelayRejected,  ///< 릴레이한 푸시를 이 빌드가 변환하지 못해 버림(열거형 × 열거자 × 이유)
        CallSite,           ///< `SW_LOG_ERROR_ONCE` · `SW_LOG_WARNING_ONCE` — 호출 지점(파일 × 줄)마다 한 번
        Count
    };

    /**
     * @struct LogOnce
     * @brief 종류와 키마다 범위 안에서 처음인지 답합니다. 여러 스레드가 함께 부릅니다.
     */
    struct SW_API LogOnce
    {
        /**
         * @brief 이번 범위에서 @p kind 의 @p key 를 처음 알리는 것이면 true 이고, 그 키를 알린 것으로 적습니다. 부른 횟수는 `getAttemptCount` 가 센다.
         * @details 키는 호출자가 이름들의 해시로 만든 값이라 서로 다른 일이 같은 키로 겹치면 두 번째 알림이 사라진다. 그 대가는 알림 한 줄이고 기능은 틀리지 않으므로
         *          원문 키를 두지 않는다(원문 키는 거절이 쏟아지는 길에서 문자열을 만들어야 한다). 결과를 바꾸는 판정에는 쓰지 않는다.
         */
        [[nodiscard]] static bool tryClaim( LogOnceKind kind, uint64 key );
        /**
         * @brief `tryClaim` 을 @p kind 로 부른 횟수입니다(처음이 아니라 알리지 않은 것도 셉니다). 거절 · 버림을 세는 지표로 씁니다.
         * @details 프로세스(엔진 수명 범위)가 사는 동안 쌓이고 `beginScope` 가 지우지 않는다 — 범위를 새로 열어도 센 횟수는 남는다.
         */
        [[nodiscard]] static uint64 getAttemptCount( LogOnceKind kind );
        /** @brief 새 범위를 엽니다 — 모든 종류의 알린 키를 잊습니다. 테스트 하니스가 케이스마다 부릅니다. */
        static void beginScope();
        /** @brief 호출 지점의 키입니다(파일 경로 해시 × 줄). `SW_LOG_*_ONCE` 가 `CallSite` 종류의 키로 쓴다. */
        [[nodiscard]] static constexpr uint64 makeSiteKey( const utf8* pFile, size_t fileLength, int32 line ) noexcept
        {
            return HashUtil::combine( HashUtil::computeStringHash64( pFile, fileLength, false ), static_cast<uint64>( line ) );
        }
    };
} // namespace sw

/**
 * @brief 이 호출 지점에서 처음 부를 때만 Error 를 남깁니다. 프레임마다 도는 길의 "필수 서비스 없음" 처럼 같은 줄이 쏟아질 곳에 씁니다.
 * @details 호출 지점마다 정적 플래그를 두지 않고 `LogOnce` 의 범위 상태(스레드 안전, 테스트 케이스마다 초기화)를 쓴다. 인자는 `SW_LOG_ERROR` 와 같다.
 */
#define SW_LOG_ERROR_ONCE( ... )                                                                                                                \
    do                                                                                                                                          \
    {                                                                                                                                           \
        if ( ::sw::LogOnce::tryClaim( ::sw::LogOnceKind::CallSite, ::sw::LogOnce::makeSiteKey( __FILE__, sizeof( __FILE__ ) - 1, __LINE__ ) ) ) \
            SW_LOG_ERROR( __VA_ARGS__ );                                                                                                        \
    } while ( 0 )
/**
 * @brief 이 호출 지점 × @p name 마다 한 번 Error 를 남깁니다. 프레임마다 도는 길이 장면 · 자원 이름에 따라 실패할 때, 호출 지점당 한 번(`SW_LOG_ERROR_ONCE`)이면 두 번째 이름이
 *        가려지므로 이름별로 한 번씩 알립니다.
 * @param name `data()` 와 `size()` 를 가진 이름(`string` · `string_view`)입니다. 나머지 인자는 `SW_LOG_ERROR` 와 같습니다.
 */
#define SW_LOG_ERROR_ONCE_FOR( name, ... )                                                                                                          \
    do                                                                                                                                              \
    {                                                                                                                                               \
        if ( ::sw::LogOnce::tryClaim( ::sw::LogOnceKind::CallSite,                                                                                  \
                                      ::sw::HashUtil::combine( ::sw::LogOnce::makeSiteKey( __FILE__, sizeof( __FILE__ ) - 1, __LINE__ ),            \
                                                               ::sw::HashUtil::computeStringHash64( ( name ).data(), ( name ).size(), false ) ) ) ) \
            SW_LOG_ERROR( __VA_ARGS__ );                                                                                                            \
    } while ( 0 )
/** @brief `SW_LOG_ERROR_ONCE` 의 Warning 판입니다. */
#define SW_LOG_WARNING_ONCE( ... )                                                                                                              \
    do                                                                                                                                          \
    {                                                                                                                                           \
        if ( ::sw::LogOnce::tryClaim( ::sw::LogOnceKind::CallSite, ::sw::LogOnce::makeSiteKey( __FILE__, sizeof( __FILE__ ) - 1, __LINE__ ) ) ) \
            SW_LOG_WARNING( __VA_ARGS__ );                                                                                                      \
    } while ( 0 )
