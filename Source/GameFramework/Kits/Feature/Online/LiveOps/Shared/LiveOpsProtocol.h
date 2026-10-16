/**
 * @file LiveOpsProtocol.h
 * @brief 라이브 운영 서비스의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kLiveOps`), 본문, 영속 레코드의 형식을 정의하며 클라이언트, 서버, 운영 도구가 함께 씁니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          업무 결과는 응답 본문(`LiveOpsReply::_result`)에 담으며 경제와 매칭 키트도 같다. 공통 오류만 오류 코드로 오고, 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 *          저장소의 이벤트 정의와 기기 레코드는 다른 빌드가 읽으므로 자기 기술형(A, `BinarySerializer`)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    /** @brief 라이브 운영 메서드와 알림 번호입니다. 와이어 값이며 온라인 스키마 해시에 들어가지 않습니다(`OnlineProtocolRegistry.h`). */
    struct LiveOpsMethod
    {
        static constexpr uint16 kGetLiveState     = OnlineMethodRange::kLiveOps + 0x01; ///< 지역과 빌드 버전을 받아 이 계정에 열린 이벤트 목록을 돌려준다. 출시 비율이 계정을 보므로 로그인한 뒤에 쓴다
        static constexpr uint16 kRegisterDevice   = OnlineMethodRange::kLiveOps + 0x02; ///< 제공자, 토큰, 언어를 받는다. 로그인한 뒤에 쓰며 같은 토큰은 덮어쓴다
        static constexpr uint16 kUnregisterDevice = OnlineMethodRange::kLiveOps + 0x03; ///< 제공자와 토큰을 받는다. 로그아웃이나 알림 끄기에서 쓴다
        static constexpr int32  kCount            = 3;

        static constexpr uint16 kPushLiveState = OnlineMethodRange::kLiveOps + 0x80; ///< 본문은 없다. 열린 이벤트 목록이 바뀌었으니 다시 받으라는 알림이다

        static_assert( OnlineMethodRange::isInRange( kUnregisterDevice, OnlineMethodRange::kLiveOps ) && OnlineMethodRange::isMethod( kUnregisterDevice ) );
        static_assert( OnlineMethodRange::isInRange( kPushLiveState, OnlineMethodRange::kLiveOps ) && OnlineMethodRange::isMethod( kPushLiveState ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 열린 이벤트 조회(`kGetLiveState`)의 요청입니다. */
    REFLECT()
    struct LiveOpsStateRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        string _region{}; ///< 상한은 `ServerRecord::kMaxNameSize` 와 같다
        PROPERTY()
        uint32 _buildVersion{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 기기 해제(`kUnregisterDevice`)의 요청입니다. 등록 요청은 `PushDeviceRegistration` 입니다. */
    REFLECT()
    struct LiveOpsDeviceKey
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        string _providerID{}; ///< 상한은 `PushLimit::kMaxProviderIDSize` 와 같다
        PROPERTY( MaxCount = 256 )
        string _token{}; ///< 상한은 `PushLimit::kMaxTokenSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 실패하면 `_result` 만 의미가 있습니다. */
    REFLECT()
    struct LiveOpsReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 256 )
        vector<LiveEventState> _listEvent{}; ///< 상한은 `LiveOpsLimit::kMaxEventCount` 와 같다
        PROPERTY()
        LiveOpsResult _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LiveOpsProtocol
     * @brief 라이브 운영 키트의 메시지 타입 목록, 등록 항목, 영속 레코드 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     */
    struct SW_KIT_API LiveOpsProtocol
    {
        using MessageTypes = OnlineMessageTypes<LiveOpsStateRequest, PushDeviceRegistration, LiveOpsDeviceKey, LiveOpsReply>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 이벤트 정의와 기기 등록의 저장소 레코드를 읽고 씁니다. 자기 기술형(A)이며 레코드 판을 머리에 둡니다.
         * @details 다른 판이거나 모르는 필드가 있으면 읽지 않습니다. 필드가 바뀌어 옛 레코드를 읽어야 하면 판을 올리고 변환(`SchemaMigrateFn`)을 등록합니다.
         *          A 는 `MaxCount` 를 보지 않으므로 서버는 읽은 이벤트 정의를 운영 쓰기와 같은 규칙(`LiveEventRules::isValid`)으로 다시 확인합니다.
         */
        static vector<uint8>      encodeEvent( const LiveEventDefinition& definition );
        [[nodiscard]] static bool decodeEvent( const vector<uint8>& bytes, LiveEventDefinition& outDefinition );
        static vector<uint8>      encodeDevice( const PushDeviceRegistration& registration );
        [[nodiscard]] static bool decodeDevice( const vector<uint8>& bytes, PushDeviceRegistration& outRegistration );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 손상된 본문은 Invalid, 도배 제한은 RateLimited, 나머지는 Unavailable 입니다. */
        static LiveOpsResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
