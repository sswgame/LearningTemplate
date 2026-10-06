/**
 * @file ActionEnemyBrain.h
 * @brief 적 패턴 실행 — 데이터로 적은 간단한 상태 기계(`ActionPatternDef`)를 프레임 단위로 돌립니다: 시간이 다 하면 다음 상태, 플레이어가 가까우면
 *        `onNear` 상태, 맞으면 `onHit` 상태(경직). 상태마다 이동 · 근접 판정 · 들어설 때 한 발 쏘기를 냅니다.
 * @details 순찰 → 조준 → 사격 → 쉬기 같은 플랫포머 졸개 · 보스 패턴이 이것으로 충분합니다. 난수가 없고 프레임으로만 가서 결정적입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ActionPatternDef;
    struct ActionPatternStateDef;

    class ActionPlatformerCatalog;
    class Archive;

    /** @brief 이번 프레임에 적이 할 일입니다. */
    struct ActionEnemyAction
    {
        float32 _moveX{ 0.0f };       ///< −1..1(바라보는 쪽을 곱한 값)
        float32 _fireSpeed{ 0.0f };   ///< 쏠 때의 탄속
        uint8   _bFire{ SW_FALSE };   ///< 이번 프레임에 한 발 쏜다
        uint8   _bAttack{ SW_FALSE }; ///< 근접 판정이 켜져 있다
        uint8   _bStateChanged{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionEnemyBrain
     * @brief 적 하나의 패턴 진행입니다. 정의는 빌려 씁니다.
     */
    class SW_GF_API ActionEnemyBrain
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "APEB" );
        static constexpr uint32 kStateVersion = 1;

        ActionEnemyBrain();

        /** @brief 패턴을 두고 시작 상태로 갑니다. 패턴이 비면 false 입니다. */
        [[nodiscard]] bool initialize( const ActionPatternDef* pPattern, int32 facing = 1 );
        /**
         * @brief 한 프레임 갑니다. 먼저 가까움(`onNear`)을, 그다음 시간(`next`)을 보고 상태를 바꾼 뒤 이번 프레임의 할 일을 냅니다.
         * @param playerDistance 플레이어까지 거리(가까움 판정)
         */
        ActionEnemyAction advanceFrame( float32 playerDistance );
        /** @brief 맞았습니다 — 지금 상태에 `onHit` 이 있으면 그리로 갑니다(다음 프레임부터). 바뀌었으면 true 입니다. */
        bool notifyHit();
        void setFacing( int32 facing ) { _facing = facing < 0 ? -1 : 1; }

        const hashed_string& getStateId() const;
        int32                getStateFrame() const { return _stateFrame; }
        int32                getFacing() const { return _facing; }

        /** @brief 패턴 id · 상태 id · 상태 안의 프레임 · 바라보는 쪽 · 들어선 프레임 표시를 씁니다. 패턴 정의는 카탈로그의 것이라 id 만 싣습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 패턴을 찾을 카탈로그를 빌립니다 — `readState` 하기 전에 묶는다(카탈로그는 뇌보다 오래 산다). */
        void bindCatalog( const ActionPlatformerCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief `writeState` 의 바이트로 바꿉니다 — 패턴은 묶은 카탈로그에서 id 로 찾습니다. 패턴이 있는데 카탈로그가 없거나, 없는 패턴 · 상태거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void                         enterState( int32 stateIndex );
        const ActionPatternStateDef* getState() const;

        const ActionPatternDef*        _pPattern;
        const ActionPlatformerCatalog* _pCatalog; ///< `readState` 가 패턴을 찾는 곳(빌림)
        int32                          _stateIndex;
        int32                          _stateFrame; ///< 지금 상태에서 지난 프레임(들어선 프레임 = 0)
        int32                          _facing;
        uint8                          _bEntered; ///< 이번 프레임에 상태에 들어섰다(쏘기 한 번)
    };
} // namespace sw
