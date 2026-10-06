/**
 * @file LiveOpsProtocol.h
 * @brief 라이브 운영 서비스의 와이어 — 메서드 · 알림 번호(영역 `OnlineMethodRange::kLiveOps`)와 몸 · 영속 레코드 형식입니다(클라이언트 · 서버 · 운영 도구가 같이 쓴다).
 * @details 응답 몸 = `LiveOpsResult` + 칸(업무 결과는 몸에 — 경제 · 매칭 키트와 같다). 공통 오류만 오류 코드로 오고 클라이언트는 `fromErrorCode` 로 결과에 맞춘다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/LiveOps/LiveOpsTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 라이브 운영 메서드 · 알림입니다. 와이어 값 — 바꾸면 `LiveOpsProtocol::kVersion` 을 올린다. */
    struct LiveOpsMethod
    {
        static constexpr uint16 kGetLiveState     = OnlineMethodRange::kLiveOps + 0x01; ///< 지역 · 빌드 판 → 이 계정에 열린 이벤트들(로그인 뒤 — 출시 비율이 계정을 본다)
        static constexpr uint16 kRegisterDevice   = OnlineMethodRange::kLiveOps + 0x02; ///< 제공자 · 토큰 · 언어(로그인 뒤 — 같은 토큰은 덮음)
        static constexpr uint16 kUnregisterDevice = OnlineMethodRange::kLiveOps + 0x03; ///< 제공자 · 토큰(로그아웃 · 알림 끄기)
        static constexpr int32  kCount            = 3;

        static constexpr uint16 kPushLiveState = OnlineMethodRange::kLiveOps + 0x80; ///< 몸 없음 — 열린 이벤트 묶음이 바뀌었다, 다시 받아라

        static_assert( OnlineMethodRange::isInRange( kUnregisterDevice, OnlineMethodRange::kLiveOps ) && OnlineMethodRange::isMethod( kUnregisterDevice ) );
        static_assert( OnlineMethodRange::isInRange( kPushLiveState, OnlineMethodRange::kLiveOps ) && OnlineMethodRange::isMethod( kPushLiveState ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 실패면 `_result` 만 뜻이 있다. */
    struct LiveOpsReply
    {
        vector<LiveEventState> _listEvent{};
        LiveOpsResult          _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LiveOpsProtocol
     * @brief 몸 코덱입니다. 읽기는 상한(`LiveOpsLimit`)을 넘거나 넘치면 false.
     */
    struct SW_GF_API LiveOpsProtocol
    {
        static constexpr uint32 kVersion      = 1;
        static constexpr uint8  kRecordFormat = 1;

        /** @brief 이벤트 정의(영속 레코드 · 운영 도구)입니다. */
        static void               writeEvent( BitWriter& outWriter, const LiveEventDefinition& definition );
        [[nodiscard]] static bool readEvent( BitReader& reader, LiveEventDefinition& outDefinition );
        static vector<uint8>      encodeEvent( const LiveEventDefinition& definition );
        [[nodiscard]] static bool decodeEvent( const vector<uint8>& bytes, LiveEventDefinition& outDefinition );

        static void               writeEventStates( BitWriter& outWriter, const vector<LiveEventState>& listEvent );
        [[nodiscard]] static bool readEventStates( BitReader& reader, vector<LiveEventState>& outListEvent );

        /** @brief 기기 등록(요청 몸 · 영속 레코드 몸 — 등록 시각 포함)입니다. */
        static void               writeDevice( BitWriter& outWriter, const PushDeviceRegistration& registration );
        [[nodiscard]] static bool readDevice( BitReader& reader, PushDeviceRegistration& outRegistration );

        static void               writeReply( BitWriter& outWriter, const LiveOpsReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, LiveOpsReply& outReply );

        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다 — 깨진 몸은 Invalid, 도배 제한은 RateLimited, 나머지는 Unavailable 입니다. */
        static LiveOpsResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
