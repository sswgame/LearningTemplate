/**
 * @file ActionEnemyBrain.h
 * @brief 적 패턴을 실행합니다. 데이터로 적은 간단한 상태 기계(`ActionPatternDef`)를 프레임 단위로 돌립니다. 시간이 다 되면 다음 상태로 가고, 플레이어가 가까우면 `onNear` 상태로, 맞으면 `onHit` 상태(경직)로 갑니다.
 *        상태마다 이동, 근접 판정, 들어설 때 한 발 쏘기를 정할 수 있습니다.
 * @details 순찰, 조준, 사격, 쉬기로 이어지는 플랫포머의 졸개와 보스 패턴은 이것으로 충분하다. 난수가 없고 프레임으로만 진행하므로 결정적이다.
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
     * @brief 적 하나의 패턴 진행 상태입니다. 정의는 빌려 씁니다.
     */
    class SW_GF_API ActionEnemyBrain
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "APEB" );
        static constexpr uint32 kStateVersion = 1;

        ActionEnemyBrain();

        /** @brief 패턴을 설정하고 시작 상태로 갑니다. 패턴이 비어 있으면 false 를 반환합니다. */
        [[nodiscard]] bool initialize( const ActionPatternDef* pPattern, int32 facing = 1 );
        /**
         * @brief 한 프레임 진행합니다. 먼저 가까움(`onNear`)을 보고 그다음 시간(`next`)을 확인해 상태를 바꾼 뒤, 이번 프레임에 할 일을 정합니다.
         * @param playerDistance 플레이어까지의 거리(가까움 판정에 쓴다)
         */
        ActionEnemyAction advanceFrame( float32 playerDistance );
        /** @brief 맞았음을 알립니다. 지금 상태에 `onHit` 이 있으면 다음 프레임부터 그 상태로 갑니다. 상태가 바뀌었으면 true 를 반환합니다. */
        bool notifyHit();
        void setFacing( int32 facing ) { _facing = facing < 0 ? -1 : 1; }

        const hashed_string& getStateID() const;
        int32                getStateFrame() const { return _stateFrame; }
        int32                getFacing() const { return _facing; }

        /** @brief 패턴 id, 상태 id, 상태 안의 프레임, 바라보는 쪽, 들어선 프레임 표시를 씁니다. 패턴 정의는 카탈로그의 것이라 id 만 싣습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 패턴을 찾을 카탈로그를 빌립니다. `readState` 하기 전에 연결해야 합니다(카탈로그는 이 객체보다 오래 삽니다). */
        void bindCatalog( const ActionPlatformerCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 패턴은 연결한 카탈로그에서 id 로 찾습니다. 패턴이 있는데 카탈로그가 없거나, 없는 패턴이나 상태이거나, 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void                         enterState( int32 stateIndex );
        const ActionPatternStateDef* getState() const;

        const ActionPatternDef*        _pPattern;
        const ActionPlatformerCatalog* _pCatalog; ///< `readState` 가 패턴을 찾는 카탈로그(빌려 온 것)
        int32                          _stateIndex;
        int32                          _stateFrame; ///< 지금 상태에서 지난 프레임(들어선 프레임이 0)
        int32                          _facing;
        uint8                          _bEntered; ///< 이번 프레임에 상태에 들어섰다(한 번만 쏘기 위한 표시)
    };
} // namespace sw
