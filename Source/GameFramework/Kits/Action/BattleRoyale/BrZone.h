/**
 * @file BrZone.h
 * @brief 자기장 — 단계마다 다음 원을 지금 원 안에서 씨앗 난수로 고르고(맵 경계 · 금지 지형 콜백), 기다렸다 줄이며, 자리마다 안 · 밖과 피해를 답합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrCatalog.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /**
     * @class IBrZoneTerrain
     * @brief 다음 원 중심 · 보급 상자 자리로 쓸 수 있는 땅인가를 게임이 답합니다(물 · 산꼭대기 금지). 맵 경계는 자기장이 따로 봅니다.
     */
    class SW_GF_API IBrZoneTerrain
    {
    public:
        IBrZoneTerrain()          = default;
        virtual ~IBrZoneTerrain() = default;

        IBrZoneTerrain( const IBrZoneTerrain& )            = default;
        IBrZoneTerrain& operator=( const IBrZoneTerrain& ) = default;

        virtual bool isZoneCenterAllowed( const float2& position ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 자기장의 지금 단계입니다. */
    enum class BrZoneStage : uint8
    {
        Waiting = 0, ///< 다음 원이 드러나 있고 아직 줄지 않는다
        Shrinking,   ///< 지금 원이 다음 원으로 보간된다
        Final        ///< 마지막 원 — 더 줄지 않는다
    };

    /** @brief 자기장에 생긴 일입니다. */
    struct BrZoneEvent
    {
        enum class Kind : uint8
        {
            NextZoneRevealed = 0, ///< _phase 단계의 다음 원이 정해졌다
            ShrinkStarted,
            ShrinkFinished,
            FinalZone ///< 마지막 단계까지 다 줄었다
        };
        float2  _center{};
        float32 _radius{ 0.0f };
        int32   _phase{ 0 };
        Kind    _kind{ Kind::NextZoneRevealed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class BrZone
     * @brief 원 하나(중심 · 반지름)와 다음 원입니다. 맵은 [0, mapSize]² 정사각형이고 좌표는 수평면(x, z → float2)입니다.
     * @details 다음 원은 늘 지금 원 안에 통째로 들어갑니다 — 중심을 지금 중심에서 (R − r) 안의 원판에서 고르고, 그 원이 맵 안이며
     *          지형 콜백이 허락할 때까지 `_centerAttempts` 번 시도합니다(다 실패하면 지금 중심). 줄어드는 동안 중심과 반지름은 선형 보간입니다.
     *          피해는 원 밖에서만, 지금 단계의 초당 피해입니다. 시간은 `update` 로만 흐르고 난수는 씨앗 하나라 같은 씨앗이면 같은 원이 나옵니다.
     */
    class SW_GF_API BrZone
    {
    public:
        BrZone();

        /** @brief 설정 · 맵 크기 · 씨앗 · 지형(빌림, nullptr 가능)을 두고 첫 원을 맵 중심에 놓은 뒤 첫 단계의 다음 원을 고릅니다. */
        void initialize( const BrZoneSettings& settings, float32 mapSize, uint32 seed, const IBrZoneTerrain* pTerrain );
        void update( float32 deltaTime );

        bool isInside( const float2& position ) const;
        /** @brief 원 가장자리 밖으로 나간 거리입니다(안이면 0). */
        float32 computeDistanceOutside( const float2& position ) const;
        /** @brief 그 자리에서 지금 받는 초당 피해입니다(안이면 0). */
        float32 computeDamagePerSecond( const float2& position ) const;
        /** @brief 지금 원 안 · 맵 안 · 지형이 허락하는 무작위 자리를 고릅니다(보급 상자). 못 찾으면 원 중심을 주고 false 입니다. */
        [[nodiscard]] bool pickPointInside( GameRandom& random, float2& outPosition ) const;
        void               drainEvents( vector<BrZoneEvent>& outListEvent );

        /** @brief 네트워크용 — 원 상태를 씁니다(설정 · 씨앗은 양쪽이 같다고 본다). */
        void writeState( BitWriter& writer ) const;
        /** @brief 네트워크용 — `writeState` 를 읽습니다. 넘치면 false 이고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( BitReader& reader );

        const float2& getCenter() const { return _center; }
        float32       getRadius() const { return _radius; }
        const float2& getNextCenter() const { return _nextCenter; }
        float32       getNextRadius() const { return _nextRadius; }
        int32         getPhaseIndex() const { return _phaseIndex; }
        BrZoneStage   getStage() const { return _stage; }
        /** @brief 지금 단계(기다림 · 줄어듦)가 끝날 때까지 남은 초입니다. */
        float32 getStageRemaining() const { return _stageRemaining; }
        float32 getMapSize() const { return _mapSize; }

    private:
        bool                  isCenterAllowed( const float2& center, float32 radius ) const;
        void                  beginPhase( int32 phaseIndex );
        const BrZonePhaseDef* findPhase() const;
        void                  pushEvent( BrZoneEvent::Kind kind );

        BrZoneSettings           _settings;
        EventBuffer<BrZoneEvent> _eventBuffer;
        const IBrZoneTerrain*    _pTerrain;
        GameRandom               _random;
        float2                   _center;
        float2                   _fromCenter; ///< 줄기 시작할 때의 원
        float2                   _nextCenter;
        float32                  _radius;
        float32                  _fromRadius;
        float32                  _nextRadius;
        float32                  _mapSize;
        float32                  _stageRemaining;
        int32                    _phaseIndex;
        BrZoneStage              _stage;
    };
} // namespace sw
