/**
 * @file LiveOpsProtocol.h
 * @brief 라이브 운영 서비스의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kLiveOps`), 본문, 영속 레코드의 형식을 정의하며 클라이언트, 서버, 운영 도구가 함께 씁니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          응답은 메서드마다 따로이다. 업무 결과(`LiveOpsDeviceReply::_result`)는 도메인 값만이고, 범위 밖 요청 · 일시 장애 · 버전은 공통 범주(`OnlineError`)로 온다.
 *          저장소의 이벤트 정의와 기기 레코드는 다른 빌드가 읽으므로 자기 기술형(A, `BinarySerializer`)이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsTypes.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

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
    /** @brief 열린 이벤트 조회(`GetLiveState`)의 요청입니다. */
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
    /** @brief 기기 해제(`UnregisterDevice`)의 요청입니다. 등록 요청은 `PushDeviceRegistration` 입니다. */
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
    /** @brief 열린 이벤트 조회(`GetLiveState`)의 응답입니다. */
    REFLECT()
    struct LiveOpsStateReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LiveOpsLimit::kMaxEventCount )
        vector<LiveEventState> _listEvent{}; ///< 상한은 `LiveOpsLimit::kMaxEventCount` 와 같다
    };

    /** @brief 기기 등록 · 해제의 응답입니다. */
    REFLECT()
    struct LiveOpsDeviceReply
    {
        REFLECT_BODY();
        PROPERTY()
        LiveOpsResult _result{ LiveOpsResult::Ok }; ///< 해제할 기기가 없으면 `NotFound`
    };
} // namespace sw

namespace sw
{
    /** @brief LiveOps 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct LiveOpsMethod : OnlineMethodTable<LiveOpsMethodName, OnlineMethodRange::kLiveOps, LiveOpsPushName>
    {
        using GetLiveState     = Method<MethodName::GetLiveState, LiveOpsStateRequest, LiveOpsStateReply>;       ///< 이 계정에 열린 이벤트 목록을 돌려준다. 출시 비율이 계정을 보므로 로그인한 뒤에 쓴다
        using RegisterDevice   = Method<MethodName::RegisterDevice, PushDeviceRegistration, LiveOpsDeviceReply>; ///< 로그인한 뒤에 쓰며 같은 토큰은 덮어쓴다
        using UnregisterDevice = Method<MethodName::UnregisterDevice, LiveOpsDeviceKey, LiveOpsDeviceReply>;     ///< 로그아웃이나 알림 끄기에서 쓴다
        using LiveState        = Push<PushName::LiveState, OnlineNoBody>;                                        ///< 열린 이벤트 목록이 바뀌었으니 다시 받으라는 알림이다
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
        using Table        = OnlineProtocolTable<LiveOpsMethod, OnlineMethodList<LiveOpsMethod::GetLiveState, LiveOpsMethod::RegisterDevice, LiveOpsMethod::UnregisterDevice>, OnlinePushList<LiveOpsMethod::LiveState>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = LiveOpsResult;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 이벤트 정의와 기기 등록의 저장소 레코드를 읽고 씁니다. 둘 다 영속 레코드이므로 자기 기술형(A)이며 레코드 버전을 머리에 둡니다.
         * @details 이 빌드가 모르는 필드는 레코드의 `_unknownProperties` 에 담고(다시 쓸 때 그대로 싣는다), 다른 버전이거나 아는 필드를 읽지 못했거나 깨졌으면 손상으로 보고 오류 로그를 남기며 false 입니다(`ServiceRecordUtil::readDurable`).
         *          기기 등록은 고정 배치형 코덱이 있어 선언된 범위(제공자 16, 토큰 256, 언어 16 바이트)도 확인합니다. 이벤트 정의는 코덱이 없으므로
         *          서버가 읽은 정의를 운영 쓰기와 같은 규칙(`LiveEventRules::isValid`)으로 다시 확인합니다.
         *          필드를 더하면 옛 레코드의 그 필드는 기본값으로 채워집니다. 필드를 빼거나 뜻을 바꾸면 버전을 올리고 데이터를 다시 씁니다.
         */
        [[nodiscard]] static bool encodeEvent( const LiveEventDef& definition, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeEvent( string_view key, const vector<uint8>& bytes, LiveEventDef& outDefinition );
        /** @brief 이벤트 정의를 리플렉션 JSON 글로 읽고 씁니다(GM 의 이벤트 운영). 읽기는 JSON 이 아니면 false 이며, 규칙 검사는 쓰기 길(`LiveEventRules::isValid`)이 합니다. */
        [[nodiscard]] static bool readEventJSON( string_view definitionJSON, LiveEventDef& outDefinition );
        static string             writeEventJSON( const LiveEventDef& definition );
        [[nodiscard]] static bool encodeDevice( const PushDeviceRegistration& registration, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeDevice( string_view key, const vector<uint8>& bytes, PushDeviceRegistration& outRegistration );
    };
} // namespace sw
