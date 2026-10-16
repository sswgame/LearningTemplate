/**
 * @file LiveOpsProtocol.h
 * @brief 라이브 운영 서비스의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kLiveOps`), 본문, 영속 레코드의 형식을 정의하며 클라이언트, 서버, 운영 도구가 함께 씁니다.
 * @details 응답 본문은 `LiveOpsResult` 뒤에 필드가 이어지는 형식이다. 업무 결과는 본문에 담으며 경제와 매칭 키트도 같다. 공통 오류만 오류 코드로 오고, 클라이언트는 `fromErrorCode` 로 이를 결과에 맞춘다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 라이브 운영 메서드와 알림 번호입니다. 와이어 값이므로 바꾸면 `LiveOpsProtocol::kVersion` 을 올려야 합니다. */
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
    /** @brief 모든 메서드의 응답입니다. 실패하면 `_result` 만 의미가 있습니다. */
    struct LiveOpsReply
    {
        vector<LiveEventState> _listEvent{};
        LiveOpsResult          _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SW_KIT_API LiveOpsProtocol
     * @brief 본문 코덱입니다. 읽을 때 상한(`LiveOpsLimit`)을 넘으면 false 를 반환합니다.
     */
    struct SW_KIT_API LiveOpsProtocol
    {
        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr uint32          kVersion      = 1;
        static constexpr uint8           kRecordFormat = 1;

        /** @brief 이벤트 정의를 읽고 씁니다. 영속 레코드와 운영 도구가 씁니다. */
        static void               writeEvent( BitWriter& outWriter, const LiveEventDefinition& definition );
        [[nodiscard]] static bool readEvent( BitReader& reader, LiveEventDefinition& outDefinition );
        static vector<uint8>      encodeEvent( const LiveEventDefinition& definition );
        [[nodiscard]] static bool decodeEvent( const vector<uint8>& bytes, LiveEventDefinition& outDefinition );

        static void               writeEventStates( BitWriter& outWriter, const vector<LiveEventState>& listEvent );
        [[nodiscard]] static bool readEventStates( BitReader& reader, vector<LiveEventState>& outListEvent );

        /** @brief 기기 등록을 읽고 씁니다. 요청 본문과 영속 레코드 본문이 같으며 등록 시각을 포함합니다. */
        static void               writeDevice( BitWriter& outWriter, const PushDeviceRegistration& registration );
        [[nodiscard]] static bool readDevice( BitReader& reader, PushDeviceRegistration& outRegistration );

        static void               writeReply( BitWriter& outWriter, const LiveOpsReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, LiveOpsReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 손상된 본문은 Invalid, 도배 제한은 RateLimited, 나머지는 Unavailable 입니다. */
        static LiveOpsResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
