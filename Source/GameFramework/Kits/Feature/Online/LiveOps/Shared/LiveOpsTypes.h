/**
 * @file LiveOpsTypes.h
 * @brief 라이브 운영 키트의 타입입니다. 결과, 반복, 이벤트 정의, 클라이언트가 받는 열린 이벤트를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 라이브 운영 요청의 결과입니다. 와이어 값이므로 순서를 바꾸지 않고 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class LiveOpsResult : uint8
    {
        Ok = 0,
        NotFound,
        Invalid,
        Unavailable,
        RateLimited,     ///< 푸시의 계정 도배 제한에 걸렸다
        UnknownProvider, ///< 푸시에서 이 서버 빌드에 없는 제공자이다
        UpdateRequired,  ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 이벤트의 반복 방식입니다. 와이어 값입니다. */
    ENUM( Count = Count )
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
    /** @brief 이벤트 매개변수 한 줄입니다. 값이 '@' 로 시작하면 원격 설정 키이며 서버가 해석해 줍니다. 상한은 `LiveOpsLimit` 와 같은 값입니다. */
    REFLECT()
    struct LiveEventParameter
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LiveOpsLimit::kMaxParameterKey )
        string _key{}; ///< `[0-9a-z_.]` 형식이며 32바이트 이하이다
        PROPERTY( MaxCount = LiveOpsLimit::kMaxParameterValue )
        string _value{}; ///< 128바이트 이하이다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이벤트 정의 하나입니다. 운영이 쓰며 영속합니다.
     * @details 저장소 레코드(`LiveOpsProtocol::encodeEvent`)는 자기 기술형(A)이라 이 `PROPERTY` 목록이 레코드 형식입니다. 와이어에는 싣지 않습니다.
     *          지역 목록(`vector<string>`)이 있어 고정 배치형 코덱은 없습니다.
     */
    REFLECT()
    struct LiveEventDefinition
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LiveOpsLimit::kMaxParameterCount )
        vector<LiveEventParameter> _listParameter{}; ///< 16개 이하이다
        PROPERTY()
        vector<string> _listRegion{}; ///< 비면 모든 지역에 열린다. 16개 이하이며 이름은 `ServerRecord::kMaxNameSize` 바이트 이하이다
        PROPERTY( MaxCount = LiveOpsLimit::kMaxIDSize )
        string _eventID{}; ///< `[0-9a-z_.]` 형식이며 48바이트 이하이다. 저장소 키로 쓴다
        PROPERTY( MaxCount = LiveOpsLimit::kMaxKindSize )
        string _kind{}; ///< 게임이 해석하는 종류 코드이다(예: "xp_boost", "shop_sale")
        PROPERTY()
        int64 _startMs{ 0 };
        PROPERTY()
        int64 _endMs{ 0 }; ///< 구간은 반열림 [시작, 끝)이다
        PROPERTY()
        int64 _activeDurationMs{ 0 }; ///< 반복 회차 하나의 길이이다. Once 는 쓰지 않는다
        PROPERTY()
        int32 _activeMinuteOfDay{ 0 }; ///< 반복 회차의 시작 시각이다. UTC 기준 0~1439분이다
        PROPERTY()
        int32 _activeDayOfWeek{ 0 }; ///< Weekly 일 때의 요일이다. 0이 월요일이다
        PROPERTY()
        int32 _rolloutBasisPoints{ 10000 };
        PROPERTY()
        uint32 _minBuildVersion{ 0 };
        PROPERTY()
        LiveEventRecurrence _recurrence{ LiveEventRecurrence::Once };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bClientVisible{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 받는 열린 이벤트입니다. 매개변수는 원격 설정까지 해석한 값입니다. 응답(`LiveOpsReply`)에 고정 배치형으로 실립니다. */
    REFLECT()
    struct LiveEventState
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LiveOpsLimit::kMaxParameterCount )
        vector<LiveEventParameter> _listParameter{}; ///< 상한은 `LiveOpsLimit::kMaxParameterCount` 와 같다
        PROPERTY( MaxCount = LiveOpsLimit::kMaxIDSize )
        string _eventID{}; ///< 상한은 `LiveOpsLimit::kMaxIDSize` 와 같다
        PROPERTY( MaxCount = LiveOpsLimit::kMaxKindSize )
        string _kind{}; ///< 상한은 `LiveOpsLimit::kMaxKindSize` 와 같다
        PROPERTY()
        int64 _windowEndMs{ 0 }; ///< 이번 회차(또는 기간)의 끝이다. 화면의 "남은 시간" 에 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 푸시 알림 하나입니다. 문구는 로컬라이제이션 키와 인자로 이루어지며, 제공자가 키와 인자를 지원하면 그대로 보내고 아니면 게임이 풀어서 넣습니다. */
    struct PushNotificationMessage
    {
        static constexpr int32 kArgumentCount = 4;

        string _titleKey{};
        string _bodyKey{};
        string _deepLink{};                    ///< 앱이 열 화면이다(예: "mailbox", "event/halloween")
        string _arrArgument[kArgumentCount]{}; ///< 본문에 들어갈 인자이다
        int32  _badgeCount{ -1 };              ///< -1이면 배지를 건드리지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 기기 등록 하나입니다. 클라이언트가 OS 에서 받은 토큰을 담습니다.
     * @details 등록 요청 본문(B)과 저장소의 기기 레코드(A, `LiveOpsProtocol::encodeDevice`)가 같은 선언을 씁니다. 상한은 `PushLimit` 와 같은 값입니다.
     */
    REFLECT()
    struct PushDeviceRegistration
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = PushLimit::kMaxProviderIDSize )
        string _providerID{}; ///< `[0-9a-z_]` 형식이며 16바이트 이하이다(예: "fake", "apns", "fcm")
        PROPERTY( MaxCount = PushLimit::kMaxTokenSize )
        string _token{}; ///< 256바이트 이하이다. 값으로만 두며 저장소 키는 토큰 해시를 쓴다
        PROPERTY( MaxCount = PushLimit::kMaxLocaleSize )
        string _locale{}; ///< 언어 코드이다(예: "ko-KR")
        PROPERTY()
        int64 _registeredMs{ 0 }; ///< 서버가 등록 시각으로 덮어쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 푸시의 상한과 백오프 값입니다. */
    struct PushLimit
    {
        static constexpr int32 kMaxProviderIDSize   = 16;
        static constexpr int32 kMaxTokenSize        = 256;
        static constexpr int32 kMaxLocaleSize       = 16;
        static constexpr int32 kMaxDevicePerAccount = 10;
        static constexpr int32 kMaxRetry            = 5;    ///< 일시 실패와 제공자 지연은 다섯 번까지 다시 시도한다
        static constexpr int64 kFirstBackoffMs      = 1000; ///< 1, 2, 4, 8, 16초로 늘어난다

        /** @brief 제공자 id 규칙(`[0-9a-z_]`, 1~16바이트)에 맞으면 true 입니다. */
        SW_KIT_API static bool isValidProviderID( string_view providerID );
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브 운영의 상한 값들입니다. */
    struct LiveOpsLimit
    {
        static constexpr int32 kMaxIDSize         = 48;
        static constexpr int32 kMaxKindSize       = 32;
        static constexpr int32 kMaxParameterCount = 16;
        static constexpr int32 kMaxParameterKey   = 32;
        static constexpr int32 kMaxParameterValue = 128;
        static constexpr int32 kMaxRegionCount    = 16;
        static constexpr int32 kMaxEventCount     = 256;

        /** @brief 이벤트 id 와 매개변수 키 규칙(`[0-9a-z_.]`, 1~@p maxSize 바이트)에 맞으면 true 입니다. */
        SW_KIT_API static bool isValidKey( string_view key, int32 maxSize );
    };
} // namespace sw
