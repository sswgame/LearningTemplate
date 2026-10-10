/**
 * @file EphemeralServerBus.h
 * @brief 캐시 층(`IEphemeralStore`)의 발행/구독 위 서버 간 버스 — 서버 여럿이 같은 캐시를 볼 때입니다.
 * @details - 채널 이름 `bus:<주제>`, 봉투 `[판 1][서버 id 8][순번 8][몸]`(리틀 엔디언).
 *          - 버스는 **자기 캐시 앞을 혼자 쓴다** — 그 앞의 답(발행 결과)과 메시지를 버스가 꺼내므로, 같은 앞을 다른 서비스와 나누면 서로의 답을 가져간다.
 *            (서비스 틀이 답 · 메시지를 요청 id · 채널별로 나눠 주게 되면 앞을 나눌 수 있다.)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IEphemeralStore;

    /**
     * @class EphemeralServerBus
     * @brief 캐시 발행/구독 위 버스 하나(서버 하나)입니다.
     */
    class SW_GF_API EphemeralServerBus final : public IServerBus
    {
    public:
        /** @brief @p pStore 는 빌려 쓴다(버스보다 오래 살고, 이 버스만 쓴다). */
        EphemeralServerBus( IEphemeralStore* pStore, uint64 serverID );

        void   publish( string_view topic, const uint8* pData, int32 size ) override;
        void   subscribe( string_view topic ) override;
        void   unsubscribe( string_view topic ) override;
        int32  pollMessages( vector<ServerBusMessage>& outListMessage ) override;
        uint64 getServerID() const override { return _serverID; }

        /** @brief 버린 봉투(깨진 것 · 판이 다른 것)의 수입니다(진단). */
        uint64 getDroppedCount() const { return _droppedCount; }

    private:
        IEphemeralStore* _pStore;
        uint64           _serverID;
        uint64           _nextSequence;
        uint64           _droppedCount;
    };
} // namespace sw
