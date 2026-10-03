/**
 * @file LagCompensation.h
 * @brief 랙 보정 — 서버가 틱마다 맞을 수 있는 몸(자리 · 반지름)을 기억해 두고, 쏜 클라이언트가 보던 시각(소수 틱)으로 되감아 맞음을 판정합니다.
 * @details "내 화면에서는 맞았는데" 를 서버가 인정하는 방식입니다(밸브 소스 엔진). 되감기 한도는 기억하는 틱 수입니다 — 너무 늦은 사격은 지금 자리로 판정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 한 틱의 몸 하나입니다. */
    struct LagRecord
    {
        float3  _position{};
        float32 _radius{ 0.5f };
        uint32  _entityId{ 0 };
    };

    class SW_GF_API LagCompensationHistory
    {
    public:
        explicit LagCompensationHistory( int32 capacity = 32 );

        void record( uint32 tick, const vector<LagRecord>& listRecord );
        /** @brief @p tick(소수)에서의 그 몸입니다 — 앞뒤 틱 사이를 섞는다. 기억 밖이면 가장 가까운 끝 틱입니다. 없으면 false 입니다. */
        bool sampleAt( float32 tick, uint32 entityId, LagRecord& outRecord ) const;
        /** @brief 그 시각의 광선 맞음(가장 가까운 몸)입니다. 안 맞으면 0 입니다. */
        uint32 raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityId, float32& outDistance ) const;
        uint32 getNewestTick() const { return _newestTick; }

    private:
        struct Frame
        {
            vector<LagRecord> _listRecord{};
            uint32            _tick{ 0xFFFFFFFFu };
        };

        const Frame*            findFrame( uint32 tick ) const;
        static const LagRecord* findRecord( const Frame& frame, uint32 entityId );

        vector<Frame> _listFrame;
        uint32        _newestTick;
        bool          _bHasFrame;
    };
} // namespace sw
