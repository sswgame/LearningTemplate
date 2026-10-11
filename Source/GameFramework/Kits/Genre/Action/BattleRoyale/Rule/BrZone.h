/**
 * @file BrZone.h
 * @brief 자기장입니다. 단계마다 다음 원을 지금 원 안에서 시드 난수로 고르고(맵 경계와 금지 지형 콜백을 확인), 기다렸다가 줄이며, 위치마다 안과 밖 여부와 피해를 답합니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BrCatalog.h"

namespace sw
{
    class Archive;
    class BitReader;
    class BitWriter;

    /**
     * @class IBrZoneTerrain
     * @brief 다음 원 중심이나 보급 상자 위치로 쓸 수 있는 땅인지 게임이 답합니다(물과 산꼭대기는 금지). 맵 경계는 자기장이 따로 확인합니다.
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
        Shrinking,   ///< 지금 원이 다음 원으로 보간되는 중이다
        Final        ///< 마지막 원이다(더 줄지 않는다)
    };

    /** @brief 자기장에서 생긴 일입니다. */
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
     * @brief 원 하나(중심과 반지름)와 다음 원입니다. 맵은 [0, mapSize]² 정사각형이고 좌표는 수평면(x, z → float2)입니다.
     * @details 다음 원은 항상 지금 원 안에 통째로 들어간다. 중심은 지금 중심에서 (R − r) 안의 원판에서 고르고, 그 원이 맵 안에 있고 지형 콜백이 허락할 때까지 `_centerAttempts` 번 시도한다(다 실패하면 지금 중심을 쓴다).
     *          줄어드는 동안 중심과 반지름은 선형 보간한다. 피해는 원 밖에서만 받고, 지금 단계의 초당 피해를 쓴다.
     *          시간은 `update` 로만 흐르고 난수는 시드 하나로 만들므로, 같은 시드면 같은 원이 나온다.
     */
    class SW_GF_API BrZone
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "BRZN" );
        static constexpr uint32 kStateVersion = 1;

        BrZone();

        /** @brief 설정, 맵 크기, 시드, 지형(빌려 옴, nullptr 가능)을 설정하고, 첫 원을 맵 중심에 놓은 뒤 첫 단계의 다음 원을 고릅니다. */
        void initialize( const BrZoneSettings& settings, float32 mapSize, uint32 seed, const IBrZoneTerrain* pTerrain );
        void update( float32 deltaTime );

        bool isInside( const float2& position ) const;
        /** @brief 원 가장자리 밖으로 나간 거리를 반환합니다(안이면 0). */
        float32 computeDistanceOutside( const float2& position ) const;
        /** @brief 그 위치에서 지금 받는 초당 피해를 반환합니다(안이면 0). */
        float32 computeDamagePerSecond( const float2& position ) const;
        /** @brief 지금 원 안이고 맵 안이며 지형이 허락하는 무작위 위치를 고릅니다(보급 상자). 못 찾으면 원 중심을 주고 false 를 반환합니다. */
        [[nodiscard]] bool pickPointInside( GameRandom& random, float2& outPosition ) const;
        void               drainEvents( vector<BrZoneEvent>& outListEvent );

        /** @brief 네트워크용으로 원 상태를 씁니다(설정과 시드는 양쪽이 같다고 가정합니다). */
        void writeState( BitWriter& writer ) const;
        /** @brief 네트워크용으로 `writeState` 가 쓴 값을 읽습니다. 값이 범위를 넘으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( BitReader& reader );
        /** @brief 세이브(`Archive`)에 넷 코덱(`writeState( BitWriter& )`)의 바이트를 길이가 붙은 본문으로 싣고 난수 상태를 붙입니다. 설정과 지형은 `initialize` 에서 받은 것이고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState( Archive& )` 가 쓴 값을 읽습니다. 본문이 잘렸거나 넷 코덱이 거절하면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

        const float2& getCenter() const { return _center; }
        float32       getRadius() const { return _radius; }
        const float2& getNextCenter() const { return _nextCenter; }
        float32       getNextRadius() const { return _nextRadius; }
        int32         getPhaseIndex() const { return _phaseIndex; }
        BrZoneStage   getStage() const { return _stage; }
        /** @brief 지금 단계(기다림 또는 줄어듦)가 끝날 때까지 남은 시간(초)입니다. */
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
