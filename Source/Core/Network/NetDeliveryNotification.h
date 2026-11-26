/**
 * @file NetDeliveryNotification.h
 * @brief 전달 통지를 단 비신뢰 메시지의 판정과 통지 하나를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 전달 통지를 단 비신뢰 메시지(`INetPeers::sendNotifiedMessage`)의 판정입니다. 메시지마다 정확히 한 번 납니다. */
    enum class NetDeliveryResult : uint8
    {
        Delivered = 0, ///< 실린 패킷을 상대가 확인했다.
        Lost           ///< 실린 패킷을 잃은 것으로 판정했거나(빠른 재전송 간격 · 패킷 창 밖), 싣지 못하고 줄에서 나이 상한을 넘겼다.
    };

    /**
     * @brief 전달 통지 하나입니다(언리얼 `PacketNotify` 의 ACK · NAK 자리). 패킷 확인을 메시지 단위로 돌려준다 — 키트가 확인 메시지를 따로 주고받지 않는다.
     * @details 종류(`_kind`)는 메시지 첫 바이트다. 라우터가 그 종류를 보내는 처리기(`INetMessageHandler::getNotifiedKindMask`)에게 넘긴다.
     */
    struct NetDeliveryNotification
    {
        uint32            _tag{ 0 };                                        ///< 보낼 때 단 값(키트가 정한다 — MMO 는 서버 틱).
        int32             _connectionID{ constant::invalid_index::kInt32 }; ///< 보낸 연결. 연결 하나(`NetConnection`)는 모르므로 엔드포인트가 채운다.
        uint8             _kind{ 0 };
        NetDeliveryResult _result{ NetDeliveryResult::Delivered };
    };
} // namespace sw
