/**
 * @file GhostEncounter.h
 * @brief 방 하나의 유령 싸움을 구현합니다. 유령 상태(숨음, 나타남, 공격, 기절, 흡입 중, 잡힘), 손전등 원뿔(각과 거리), 모아 쏘는 스트로브,
 *        청소기 흡입 줄다리기(유령 체력, 도망 방향, 반대로 당길 때의 보너스 피해와 서지 게이지, 끌려감, 강화 단계)를 다룹니다.
 * @details 유령의 위치는 게임이 움직이고 `setGhostPosition` 으로 알린다. 이 클래스는 규칙만 가진다. 원뿔 판정은 기반 `RayMath::isInFlatCone`
 *          (높이를 무시하는 XZ 원뿔)을 쓴다. 흡입 중 도망 방향은 시드를 고정한 `GameRandom` 이 정하므로, 같은 시드와 같은 입력이면 같은 싸움이 된다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GhostDef;

    class Archive;
    class GhostCatalog;

    /** @brief 유령의 상태입니다. */
    enum class GhostState : uint8
    {
        Hidden = 0, ///< 보이지 않는다. 빛도 청소기도 닿지 않는다
        Visible,    ///< 나타났다. 스트로브에 기절한다
        Attacking,  ///< 공격 동작 중이다. 심장이 드러나 보통 손전등에도 기절한다
        Stunned,    ///< 기절했다. 청소기로 빨아들일 수 있다
        Sucking,    ///< 흡입 중이다. 줄다리기를 한다
        Caught
    };

    SW_GF_API const utf8* toString( GhostState state );

    /** @brief 싸움 중인 유령 하나입니다. */
    struct GhostInstance
    {
        const GhostDef* _pDef{ nullptr };
        float3          _position{};
        float3          _fleeDirection{}; ///< 흡입 중 도망치는 쪽이다(XZ 단위 벡터)
        float32         _hp{ 0.0f };
        Countdown       _timer{}; ///< 지금 상태의 남은 시간
        uint32          _id{ 0 };
        GhostState      _state{ GhostState::Hidden };
    };
} // namespace sw

namespace sw
{
    /** @brief 유령 이벤트의 종류입니다. */
    enum class GhostEventType : uint8
    {
        Appeared = 0,
        AttackLanded, ///< `_amount` 는 피해이다(맞았는지는 거리와 무적을 보고 게임이 판단한다)
        Stunned,
        SuctionStarted,
        Escaped, ///< 흡입이 끊겨 달아났다(체력은 남는다)
        Caught,  ///< `_coins` 는 떨어뜨린 동전이다
        Surge    ///< `_amount` 는 서지 피해이다
    };

    /** @brief 유령 이벤트 하나입니다. */
    struct GhostEvent
    {
        float32        _amount{ 0.0f };
        uint32         _ghostID{ 0 };
        int32          _coins{ 0 };
        GhostEventType _type{ GhostEventType::Appeared };
    };
} // namespace sw

namespace sw
{
    /** @brief 흡입 한 틱의 결과입니다. */
    struct GhostSuctionTick
    {
        float3  _drag{}; ///< 이번 틱에 플레이어가 끌려간 거리이다(게임이 플레이어를 옮긴다)
        float32 _damage{ 0.0f };
        float32 _alignment{ 0.0f }; ///< 당기는 방향과 도망 반대 방향의 내적이다(-1..1)
        uint8   _bAligned{ SW_FALSE };
        uint8   _bCaught{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GhostEncounter
     * @brief 한 방의 유령들과 손전등, 청소기를 관리합니다. 흡입은 한 번에 한 유령에게만 합니다.
     */
    class SW_GF_API GhostEncounter
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "GHEN" );
        static constexpr uint32 kStateVersion = 1;

        GhostEncounter();

        void initialize( const GhostCatalog* pCatalog, uint32 seed );
        /** @brief 유령을 모두 치웁니다(방을 나갔을 때). 흡입도 끊습니다. */
        void clear();
        /** @brief 유령을 숨은 채로 만들고 유령 번호(1부터)를 반환합니다. 모르는 종류이면 0 입니다. */
        uint32 spawnGhost( const hashed_string& ghostID, const float3& position );
        void   setGhostPosition( uint32 ghostID, const float3& position );

        /** @brief 보통 손전등을 비춥니다. 원뿔 안에서 공격 중인(심장이 드러난) 유령이 기절하며, 기절시킨 수를 반환합니다. */
        int32 shineBeam( const float3& eye, const float3& forward );
        /** @brief 스트로브를 모읍니다(버튼을 누르는 동안). */
        void chargeStrobe( float32 deltaTime );
        /** @brief 스트로브를 터뜨립니다. 다 모였으면 넓은 원뿔 안에서 나타났거나 공격 중인 유령이 기절합니다. 기절시킨 수를 반환하며(덜 모였으면 0) 모은 것은 비웁니다. */
        int32 releaseStrobe( const float3& eye, const float3& forward );
        /** @brief 이 위치가 손전등(@p bStrobe 이면 스트로브) 원뿔 안에 있는지 반환합니다. */
        bool isInCone( const float3& eye, const float3& forward, const float3& position, bool bStrobe ) const;

        /** @brief 기절한 유령을 빨아들이기 시작합니다. 거리 밖이거나, 기절 상태가 아니거나, 이미 흡입 중이면 false 를 반환합니다. */
        [[nodiscard]] bool startSuction( uint32 ghostID, const float3& playerPosition );
        /** @brief 흡입을 그만둡니다(버튼을 뗐을 때). 유령은 달아나 숨습니다. */
        void stopSuction();
        /**
         * @brief 흡입 한 틱을 진행합니다. @p pullDirection 은 플레이어가 당기는 방향(XZ 평면이며 스틱 입력)입니다. 도망 방향의 반대로 당기면 보너스 피해, 서지 게이지, 덜 끌리는 효과가 있습니다.
         */
        GhostSuctionTick updateSuction( const float3& pullDirection, float32 deltaTime );
        /** @brief 서지 게이지가 찼으면 한 번에 큰 피해를 줍니다. 흡입 중이 아니거나 덜 찼으면 false 를 반환합니다. */
        bool triggerSurge();
        /** @brief 청소기 강화 단계를 정합니다(없는 단계는 마지막 단계로 자릅니다). */
        void setVacuumStage( int32 stage );
        /** @brief 시간을 진행합니다. 숨음, 나타남, 공격, 기절 시간과 흡입 중 도망 방향 바꾸기를 처리합니다. */
        void update( float32 deltaTime );
        /** @brief 쌓인 이벤트를 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<GhostEvent>& outListEvent );

        /** @brief 난수, 유령(정의 id, 위치, 도망 방향, 체력, 상태 시간, 번호, 상태), 스트로브 충전, 서지, 흡입 대상, 다음 번호, 강화 단계를 씁니다. 카탈로그는 저장하지 않고, 이벤트는 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌거나 카탈로그에 없는 유령이면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        const GhostInstance*         findGhost( uint32 ghostID ) const;
        const vector<GhostInstance>& getGhosts() const { return _listGhost; }
        int32                        countRemaining() const;
        uint32                       getSuctionTarget() const { return _suctionTarget; }
        float32                      getStrobeCharge() const { return _strobeCharge; }
        float32                      getSurgeGauge() const { return _surgeGauge; }
        int32                        getVacuumStage() const { return _vacuumStage; }
        float32                      computeVacuumPower() const;

    private:
        GhostInstance* findGhostMutable( uint32 ghostID );
        void           enterState( GhostInstance& ghost, GhostState state );
        void           chooseFleeDirection( GhostInstance& ghost );
        void           applySuctionDamage( GhostInstance& ghost, float32 damage );
        void           pushEvent( GhostEventType type, uint32 ghostID, float32 amount = 0.0f, int32 coins = 0 );

        const GhostCatalog*     _pCatalog;
        GameRandom              _random;
        vector<GhostInstance>   _listGhost;
        EventBuffer<GhostEvent> _eventBuffer;
        float32                 _strobeCharge;
        float32                 _surgeGauge; ///< 0..1
        uint32                  _suctionTarget;
        uint32                  _nextGhostID;
        int32                   _vacuumStage;
    };
} // namespace sw
