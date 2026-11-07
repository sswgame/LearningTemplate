/**
 * @file LagCompensation.h
 * @brief 랙 보정을 합니다. 서버가 틱마다 맞을 수 있는 몸(위치와 반지름)을 기억해 두고, 쏜 클라이언트가 보던 시각(소수 틱)으로 되감아 명중을 판정합니다.
 * @details "내 화면에서는 맞았는데" 를 서버가 인정하는 방식으로, 밸브의 소스 엔진이 쓴다(언리얼 Lyra 의 서버 되감기와 같은 자리, 기억에 의존).
 *          되감기 한도는 기억하는 틱 수이다. 너무 늦은 사격은 기억하는 가장 오래된 틱으로 판정한다.
 *          몸은 위치 · 반지름 · 높이의 세로 캡슐이다(높이 0 이면 구). 뼈마다의 히트박스를 되감지 않는다 — 부르는 쪽이 되감은 차이만큼 광선을 옮겨 실제 히트박스에 다시 묻는다(`Shooter3D` `ShooterNetPawnComponent`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 한 틱의 몸 하나입니다. */
    struct LagRecord
    {
        float3  _position{}; ///< 구의 중심(`_height` 가 0), 캡슐의 발(`_height` 가 있으면)
        float32 _radius{ 0.5f };
        float32 _height{ 0.0f }; ///< 발에서 캡슐 꼭대기까지. 반지름의 두 배보다 작으면 발 위의 구 하나다. 0 이면 `_position` 이 중심인 구다
        uint32  _entityID{ 0 };
    };
} // namespace sw

namespace sw
{
    class SW_KIT_API LagCompensationHistory
    {
    public:
        explicit LagCompensationHistory( int32 capacity = 32 );

        void record( uint32 tick, const vector<LagRecord>& listRecord );
        /** @brief @p tick(소수)에서의 그 몸을 찾습니다. 앞뒤 틱 사이를 보간하며, 기억 범위 밖이면 가장 가까운 끝 틱을 씁니다. 몸이 없으면 false 를 반환합니다. */
        bool sampleAt( float32 tick, uint32 entityID, LagRecord& outRecord ) const;
        /** @brief 그 시각의 광선 명중을 판정해 가장 가까운 몸을 반환합니다(@p direction 은 단위 벡터). 맞은 몸이 없으면 0을 반환하고 @p outDistance 는 @p maxDistance 입니다. 광선의 출발점이 몸 안이면 그 몸은 맞지 않습니다. */
        uint32 raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID, float32& outDistance ) const;
        uint32 getNewestTick() const { return _listFrame.getNewestTick(); }

    private:
        static const LagRecord* findRecord( const vector<LagRecord>& listRecord, uint32 entityID );
        /** @brief 광선과 몸의 첫 교차 거리입니다. 닿지 않으면 false 입니다. */
        [[nodiscard]] static bool intersectBody( const LagRecord& body, const float3& origin, const float3& direction, float32& outDistance );

        TickRingBuffer<vector<LagRecord>> _listFrame; ///< 틱마다 맞을 수 있는 몸 목록이다. 가장 새 틱이 기록한 틱 중 가장 큰 것이다
    };
} // namespace sw
