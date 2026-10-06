/**
 * @file ServerRegistration.h
 * @brief 이 서버 프로세스를 등록합니다 — 하트비트 주기마다(상태 · 부하가 바뀌면 바로) 기록(시한)과 종류 색인을 다시 쓰고, 정상 종료 때 지웁니다.
 * @details 서비스 스레드 하나(라우터와 같은 스레드)에서 쓴다. 쓰기의 답은 보지 않는다 — 놓친 쓰기는 다음 하트비트가 메운다.
 *          프로세스가 죽으면 기록은 시한(`kRecordTtlMs`) 뒤 사라지고 색인의 낡은 멤버는 읽는 쪽(`ServerRegistryReader`)이 지운다.
 *          오케스트레이터(Agones · 쿠버네티스) 없이 두 플랫폼에서 도는 "서버가 스스로 올리는 하트비트" 모양이다 — 붙이면 등록만 그쪽 상태로 바꾼다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/GameFrameworkExports.h"

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
        static constexpr int64 kRecordTtlMs       = 15000; ///< 하트비트 셋을 놓치면 사라진다

        ServerRegistration();

        /** @brief 종류 · 지역 이름이 규칙 밖이거나 서버 id 가 0 이거나 주소가 길면 오류 로그와 함께 false 입니다. 첫 기록은 다음 `tick` 에. */
        [[nodiscard]] bool initialize( EphemeralStoreRouter* pRouter, const ServerDescriptor& descriptor );
        /** @brief 기록과 색인을 지웁니다(정상 종료 — 다른 서버의 목록에서 바로 사라진다). */
        void shutdown();

        /** @brief 하트비트 주기가 됐거나 상태 · 부하가 바뀌었으면 다시 씁니다. */
        void tick( int64 nowMs );

        void setState( ServerState state );
        void setLoad( int32 load );

        const ServerStatus& getStatus() const { return _status; }

    private:
        void writeRecord( int64 nowMs );

        ServerStatus          _status;
        EphemeralStoreRouter* _pRouter;
        int64                 _lastWrittenMs;
        uint8                 _bDirty;
    };
} // namespace sw
