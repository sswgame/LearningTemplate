/**
 * @file ServerRegistration.h
 * @brief 이 서버 프로세스를 등록합니다. 하트비트 주기마다(상태나 부하가 바뀌면 바로) 기록(시한)과 종류 색인을 다시 쓰고, 정상 종료 때 지웁니다.
 * @details 서비스 스레드 하나(라우터와 같은 스레드)에서 쓴다. 쓰기의 답은 보지 않는다. 놓친 쓰기는 다음 하트비트가 메운다.
 *          프로세스가 죽으면 기록은 시한(`kRecordTTLMs`) 뒤 사라지고 색인의 낡은 멤버는 읽는 쪽(`ServerRegistryReader`)이 지운다.
 *          오케스트레이터(Agones, 쿠버네티스) 없이 두 플랫폼에서 도는 "서버가 스스로 보내는 하트비트" 구조다. 오케스트레이터를 연결하면 등록만 그쪽 상태로 바꾼다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"

namespace sw
{
    class EphemeralStoreRouter;

    /**
     * @class ServerRegistration
     * @brief 서버 하나의 등록입니다.
     */
    class SW_GF_API ServerRegistration
    {
    public:
        static constexpr int64 kHeartbeatPeriodMs = 5000;
        static constexpr int64 kRecordTTLMs       = 15000; ///< 하트비트 셋을 놓치면 사라진다

        ServerRegistration();

        /** @brief 종류 · 지역 이름이 규칙 밖이거나 서버 id 가 0 이거나 주소가 길거나 게임 포트 수가 1 ~ `kMaxPortCount` 밖(또는 끝 포트가 65535 를 넘으면)이면 오류 로그와 함께 false 입니다. 첫 기록은 다음 `tick` 에. */
        [[nodiscard]] bool initialize( EphemeralStoreRouter* pRouter, const ServerDescriptor& descriptor );
        /** @brief 기록과 색인을 지웁니다. 정상 종료 때 불러 다른 서버의 목록에서 바로 사라지게 합니다. */
        void shutdown();

        /** @brief 하트비트 주기가 됐거나 상태 · 부하가 바뀌었으면 다시 씁니다. */
        void update( UnixMilliseconds nowMs );

        void setState( ServerState state );
        void setLoad( int32 load );
        /** @brief 정원을 바꿉니다(게임 서버가 씬을 연 뒤 자리 수를 정하는 매치 서버 — Agones SDK `SetPlayerCapacity` 와 같은 자리). */
        void setCapacity( int32 capacity );
        /**
         * @brief 예약된 좌석 @p seatCount 개가 들어와 부하(`setLoad`)에 잡혔음을 알립니다 — 서버 사이의 예약 셈에서 뺍니다. 예약 창(@p reservationTTLMs)이 지났으면 셈은 이미 없다.
         * @details 부하와 예약을 둘 다 세면 같은 사람이 두 번 자리를 차지한다. 부하를 올린 뒤 부른다.
         */
        void confirmSeats( int32 seatCount, int64 reservationTTLMs );

        const ServerStatus& getStatus() const { return _status; }

    private:
        void writeRecord( UnixMilliseconds nowMs );

    private:
        ServerStatus          _status;
        EphemeralStoreRouter* _pRouter;
        UnixMilliseconds      _lastWrittenMs;
        uint8                 _bDirty;
    };
} // namespace sw
