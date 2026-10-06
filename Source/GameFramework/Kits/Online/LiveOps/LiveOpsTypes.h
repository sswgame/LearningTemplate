/**
 * @file LiveOpsTypes.h
 * @brief 라이브 운영 키트의 타입 — 결과 · 반복 · 이벤트 정의 · 클라이언트가 받는 열린 이벤트입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 라이브 운영 요청의 결과입니다. 와이어 값이라 순서를 바꾸지 않는다(끝에만 더한다). */
    enum class LiveOpsResult : uint8
    {
        Ok = 0,
        NotFound,
        Invalid,
        Unavailable,
        RateLimited,     ///< 푸시 — 계정 도배 제한
        UnknownProvider, ///< 푸시 — 이 서버 빌드에 없는 제공자
        Count
    };

    SW_GF_API const utf8* toString( LiveOpsResult result );
} // namespace sw

namespace sw
{
    /** @brief 이벤트의 반복입니다. 와이어 값이다. */
    enum class LiveEventRecurrence : uint8
    {
        Once = 0,
        Daily,
        Weekly,
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 이벤트 매개변수 한 줄입니다. 값이 '@' 로 시작하면 원격 설정 키다(서버가 풀어 준다). */
    struct LiveEventParameter
    {
        string _key{};   ///< `[0-9a-z_.]`, 32 바이트 이하
        string _value{}; ///< 128 바이트 이하
    };
} // namespace sw

namespace sw
{
    /** @brief 이벤트 정의 하나(운영이 쓴다 — 영속)입니다. */
    struct LiveEventDefinition
    {
        vector<LiveEventParameter> _listParameter{}; ///< 16 개 이하
        vector<string>             _listRegion{};    ///< 비면 모든 지역
        string                     _eventId{};       ///< `[0-9a-z_.]`, 48 바이트 이하 — 저장소 키
        string                     _kind{};          ///< 게임이 해석하는 종류 코드("xp_boost" · "shop_sale")
        int64                      _startMs{ 0 };
        int64                      _endMs{ 0 };             ///< 반열림 [시작, 끝)
        int64                      _activeDurationMs{ 0 };  ///< 반복 회차 하나의 길이(Once 는 쓰지 않음)
        int32                      _activeMinuteOfDay{ 0 }; ///< 반복 회차 시작(UTC 0..1439)
        int32                      _activeDayOfWeek{ 0 };   ///< Weekly — 0 = 월요일
        int32                      _rolloutBasisPoints{ 10000 };
        uint32                     _minBuildVersion{ 0 };
        LiveEventRecurrence        _recurrence{ LiveEventRecurrence::Once };
        uint8                      _bClientVisible{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 받는 열린 이벤트 — 매개변수는 원격 설정까지 푼 값입니다. */
    struct LiveEventState
    {
        vector<LiveEventParameter> _listParameter{};
        string                     _eventId{};
        string                     _kind{};
        int64                      _windowEndMs{ 0 }; ///< 이번 회차(또는 기간)의 끝 — 화면의 "남은 시간"
    };
} // namespace sw

namespace sw
{
    /** @brief 푸시 알림 하나 — 글은 로컬라이제이션 키와 인자(제공자가 키 · 인자를 지원하면 그대로, 아니면 게임이 풀어 넣는다)입니다. */
    struct PushNotificationMessage
    {
        static constexpr int32 kArgumentCount = 4;

        string _titleKey{};
        string _bodyKey{};
        string _deepLink{};                    ///< 앱이 열 화면("mailbox" · "event/halloween")
        string _arrArgument[kArgumentCount]{}; ///< 본문 인자
        int32  _badgeCount{ -1 };              ///< −1 = 건드리지 않음
    };
} // namespace sw

namespace sw
{
    /** @brief 기기 등록 하나 — 클라이언트가 OS 에서 받은 토큰입니다. */
    struct PushDeviceRegistration
    {
        string _providerId{}; ///< `[0-9a-z_]` 16 바이트 — "fake" · "apns" · "fcm"
        string _token{};      ///< 256 바이트 이하(값으로만 둔다 — 저장소 키는 토큰 해시)
        string _locale{};     ///< "ko-KR"
        int64  _registeredMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 푸시의 상한 · 물러남입니다. */
    struct PushLimit
    {
        static constexpr int32 kMaxProviderIdSize   = 16;
        static constexpr int32 kMaxTokenSize        = 256;
        static constexpr int32 kMaxLocaleSize       = 16;
        static constexpr int32 kMaxDevicePerAccount = 10;
        static constexpr int32 kMaxRetry            = 5;    ///< 일시 실패 · 제공자 늦춤은 다섯 번까지
        static constexpr int64 kFirstBackoffMs      = 1000; ///< 1 · 2 · 4 · 8 · 16 초

        /** @brief 제공자 id 규칙(`[0-9a-z_]`, 1..16 바이트)인가입니다. */
        SW_GF_API static bool isValidProviderId( string_view providerId );
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브 운영의 상한입니다. */
    struct LiveOpsLimit
    {
        static constexpr int32 kMaxIdSize         = 48;
        static constexpr int32 kMaxKindSize       = 32;
        static constexpr int32 kMaxParameterCount = 16;
        static constexpr int32 kMaxParameterKey   = 32;
        static constexpr int32 kMaxParameterValue = 128;
        static constexpr int32 kMaxRegionCount    = 16;
        static constexpr int32 kMaxEventCount     = 256;

        /** @brief 이벤트 id · 매개변수 키 규칙(`[0-9a-z_.]`, 1..@p maxSize 바이트)인가입니다. */
        SW_GF_API static bool isValidKey( string_view key, int32 maxSize );
    };
} // namespace sw
