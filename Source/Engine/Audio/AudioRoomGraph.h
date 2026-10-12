/**
 * @file AudioRoomGraph.h
 * @brief 방과 포털로 측정하는 소리 가림입니다. 기하(레이캐스트) 없이 "소리가 문을 거쳐 오는가"로 가림을 정합니다 — Wwise 의 Rooms & Portals 에 해당합니다.
 * @details 방은 축 정렬 상자이고, 포털은 두 방(또는 방과 바깥)을 잇는 열림 정도(0 = 닫힘 문, 1 = 열린 문)를 가진 통로입니다. 리스너와 에미터가 같은 방이면 가림 0 입니다.
 *          다른 방이면 포털 사슬 가운데 투과율(포털마다 `열림 × (1 − 열린 포털 가림)` 의 곱)이 가장 큰 길을 골라 `가림 = 1 − max(투과율, 벽 누설)` 로 돌려줍니다.
 *          길이 없으면 벽 누설만 남습니다. 문을 여닫는 게임 코드는 `setPortalOpenness` 만 부르면 되고, 엔진의 가림 추종(`_smoothingSeconds`)이 부드럽게 바꿔 줍니다.
 *
 *          게임 스레드에서만 부릅니다(`SceneAudio::setOcclusionQuery` 로 물립니다). 길 찾기는 작은 그래프를 O(방²) 로 훑고, 작업 버퍼는 멤버를 재사용해 할당하지 않습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Audio/AudioSpatial.h"

namespace sw
{
    /**
     * @class AudioRoomGraph
     * @brief 방 · 포털 그래프와 그 위의 가림 질의입니다.
     */
    class SW_API AudioRoomGraph final : public IAudioOcclusionQuery
    {
    public:
        /** @brief 포털의 한쪽이 "어떤 방도 아닌 바깥"일 때 쓰는 방 번호입니다. */
        static constexpr uint32 kOutside = invalid_index::kUint32;

        AudioRoomGraph();
        ~AudioRoomGraph() override = default;

        /** @brief 방(축 정렬 상자)을 더하고 번호를 돌려줍니다. 방이 겹치면 부피가 작은 쪽이 그 점의 방입니다(방 안의 작은 방). */
        uint32 addRoom( const float3& center, const float3& halfExtents );
        /** @brief 두 방을 잇는 포털을 더하고 번호를 돌려줍니다. 한쪽을 `kOutside` 로 두면 바깥과 이어집니다. 방 번호가 틀리면 `invalid_index::kUint32` 입니다. */
        uint32 addPortal( uint32 roomA, uint32 roomB, float32 openness = 1.0f );
        /** @brief 포털 열림(0..1)을 바꿉니다. 없는 포털이면 false 입니다. */
        bool setPortalOpenness( uint32 portalIndex, float32 openness );
        /** @brief 포털 열림입니다. 없는 포털이면 0 입니다. */
        float32 getPortalOpenness( uint32 portalIndex ) const;
        /** @brief 열린 포털 하나가 깎는 투과율 비율(0..1, 기본 0.15)입니다. 소리가 문을 돌아 들어올 때 생기는 손실입니다. */
        void setOpenPortalOcclusion( float32 occlusion );
        /** @brief 포털 없이 벽을 통과하는 투과율(0..1, 기본 0)입니다. 가림은 이 값보다 작아지지 않습니다. */
        void setWallLeak( float32 leak );
        /** @brief 방과 포털을 모두 지웁니다. */
        void clear();

        /** @brief 점이 속한 방 번호입니다. 어느 방에도 없으면 `kOutside` 입니다. */
        uint32 findRoom( const float3& point ) const;
        /** @brief 방 수입니다. */
        uint32 getRoomCount() const { return static_cast<uint32>( _listRoom.size() ); }
        /** @brief 포털 수입니다. */
        uint32 getPortalCount() const { return static_cast<uint32>( _listPortal.size() ); }

        /** @brief 리스너 → 에미터의 가림(0 = 같은 방이거나 열린 길, 1 = 길이 없음)입니다. */
        float32 computeOcclusion( const float3& listener, const float3& emitter ) const override;

    private:
        struct Room
        {
            float3  _center{};
            float3  _halfExtents{};
            float32 _volume{ 0.0f };
        };

        struct Portal
        {
            uint32  _roomA{ 0 };
            uint32  _roomB{ 0 };
            float32 _openness{ 1.0f };
        };

        /** @brief 방 번호를 길 찾기 노드 번호로 바꿉니다(바깥은 마지막 노드). */
        uint32 toNode( uint32 room ) const;
        /** @brief 두 노드 사이 투과율이 가장 큰 길의 값입니다(0 = 길 없음). */
        float32 computeBestTransmittance( uint32 fromNode, uint32 targetNode ) const;

    private:
        vector<Room>            _listRoom;
        vector<Portal>          _listPortal;
        float32                 _openPortalOcclusion;
        float32                 _wallLeak;
        mutable vector<float32> _listTransmittance; ///< 길 찾기 작업 버퍼(노드마다 지금까지의 최대 투과율)
        mutable vector<uint8>   _listVisited;       ///< 길 찾기 작업 버퍼
    };
} // namespace sw
