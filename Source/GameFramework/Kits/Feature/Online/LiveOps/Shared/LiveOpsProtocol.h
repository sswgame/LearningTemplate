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

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    /** @brief 라이브 운영 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class LiveOpsMethodName : uint8
    {
        GetLiveState,
        RegisterDevice,
        UnregisterDevice,
        Count,
    };

    /** @brief 라이브 운영 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class LiveOpsPushName : uint8
    {
        LiveState,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브 운영 메서드와 알림 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct LiveOpsMethod : OnlineMethodTable<LiveOpsMethodName, OnlineMethodRange::kLiveOps, LiveOpsPushName>
    {
        static constexpr uint16 kGetLiveState     = toMethod( LiveOpsMethodName::GetLiveState );     ///< 지역과 빌드 버전을 받아 이 계정에 열린 이벤트 목록을 돌려준다. 출시 비율이 계정을 보므로 로그인한 뒤에 쓴다
        static constexpr uint16 kRegisterDevice   = toMethod( LiveOpsMethodName::RegisterDevice );   ///< 제공자, 토큰, 언어를 받는다. 로그인한 뒤에 쓰며 같은 토큰은 덮어쓴다
        static constexpr uint16 kUnregisterDevice = toMethod( LiveOpsMethodName::UnregisterDevice ); ///< 제공자와 토큰을 받는다. 로그아웃이나 알림 끄기에서 쓴다
        static constexpr uint16 kPushLiveState    = toPush( LiveOpsPushName::LiveState );            ///< 본문은 없다. 열린 이벤트 목록이 바뀌었으니 다시 받으라는 알림이다
    };
} // namespace sw

namespace sw
{
    /** @brief 열린 이벤트 조회(`kGetLiveState`)의 요청입니다. */
    REFLECT()
    struct LiveOpsStateRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
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
        PROPERTY( MaxCount = PushLimit::kMaxProviderIDSize )
        string _providerID{}; ///< 상한은 `PushLimit::kMaxProviderIDSize` 와 같다
        PROPERTY( MaxCount = PushLimit::kMaxTokenSize )
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
        PROPERTY( MaxCount = LiveOpsLimit::kMaxEventCount )
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
         * @brief 이벤트 정의와 기기 등록의 저장소 레코드를 읽고 씁니다. 둘 다 영속 레코드이므로 자기 기술형(A)이며 레코드 판을 머리에 둡니다.
         * @details 다른 판이거나 모르는 필드가 있거나 깨졌으면 손상으로 보고 오류 로그를 남기며 false 입니다(`ServiceRecordUtil::readDurable`).
         *          기기 등록은 고정 배치형 코덱이 있어 선언된 범위(제공자 16, 토큰 256, 언어 16 바이트)도 확인합니다. 이벤트 정의는 코덱이 없으므로
         *          서버가 읽은 정의를 운영 쓰기와 같은 규칙(`LiveEventRules::isValid`)으로 다시 확인합니다.
         *          필드를 더하면 옛 레코드의 그 필드는 기본값으로 채워집니다. 필드를 빼거나 뜻을 바꾸면 판을 올리고 데이터를 다시 씁니다.
         */
        [[nodiscard]] static bool encodeEvent( const LiveEventDefinition& definition, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeEvent( string_view key, const vector<uint8>& bytes, LiveEventDefinition& outDefinition );
        [[nodiscard]] static bool encodeDevice( const PushDeviceRegistration& registration, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeDevice( string_view key, const vector<uint8>& bytes, PushDeviceRegistration& outRegistration );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 손상된 본문은 Invalid, 도배 제한은 RateLimited, 나머지는 Unavailable 입니다. */
        static LiveOpsResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
