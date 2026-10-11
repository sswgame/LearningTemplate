/**
 * @file LagCompensation.h
 * @brief 랙 보정을 합니다. 서버가 틱마다 맞을 수 있는 몸(위치와 반지름)을 기억해 두고, 쏜 클라이언트가 보던 시각(소수 틱)으로 되감아 명중을 판정합니다.
 * @details "내 화면에서는 맞았는데" 를 서버가 인정하는 방식으로, 밸브의 소스 엔진이 쓴다.
 *          되감기 한도는 기억하는 틱 수이다. 너무 늦은 사격은 현재 위치로 판정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 한 틱의 몸 하나입니다. */
    struct LagRecord
    {
        float3  _position{};
        float32 _radius{ 0.5f };
        uint32  _entityID{ 0 };
    };
} // namespace sw

namespace sw
{
    class SW_GF_API LagCompensationHistory
    {
    public:
        explicit LagCompensationHistory( int32 capacity = 32 );

        void record( uint32 tick, const vector<LagRecord>& listRecord );
        /** @brief @p tick(소수)에서의 그 몸을 찾습니다. 앞뒤 틱 사이를 보간하며, 기억 범위 밖이면 가장 가까운 끝 틱을 씁니다. 몸이 없으면 false 를 반환합니다. */
        bool sampleAt( float32 tick, uint32 entityID, LagRecord& outRecord ) const;
        /** @brief 그 시각의 광선 명중을 판정해 가장 가까운 몸을 반환합니다. 맞은 몸이 없으면 0을 반환합니다. */
        uint32 raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID, float32& outDistance ) const;
        uint32 getNewestTick() const { return _listFrame.getNewestTick(); }

    private:
        static const LagRecord* findRecord( const vector<LagRecord>& listRecord, uint32 entityID );

        TickRingBuffer<vector<LagRecord>> _listFrame; ///< 틱마다 맞을 수 있는 몸 목록이다. 가장 새 틱이 기록한 틱 중 가장 큰 것이다
    };
} // namespace sw
