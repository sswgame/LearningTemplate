/**
 * @file PlayerController.h
 * @brief 타일 스텝 플레이어 이동 + locomotion FSM (Playing 전용 입력)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Overworld/PlayerLocomotion.h"

namespace sw
{
    class ActionMap;
    class InputManager;
    class TileMap;

    // ------------------------------------------------------------------------------
    // 1) PlayerController — 입력 → 한 칸 이동 → 워프/조우/상호작용 플래그
    //    전환 중에는 setInputEnabled(false)
    // ------------------------------------------------------------------------------
    /**
     * @brief 이번 걸음에 야생 조우가 나는지 — `PlayerController` 가 쓰는 판정 그대로입니다.
     * @param encounterRate 0 ~ 1. **0 이면 언제나 false** 이고, 1 이상이면 매 걸음 true 다.
     * @param stepCount 조우 타일을 밟은 누적 걸음 수(1 부터).
     * @details 예전에는 이 판정이 `tryStep` 안에 한 줄로 묻혀 있었고, `rate <= 0.01` 을 주기
     *          3 으로 바꿔 놓아서 **끄려고 넣은 0 이 세 걸음마다 켜는 값**이었다. 밖으로 꺼내
     *          이름을 주고 테스트가 직접 물을 수 있게 한다.
     */
    SW_GF_API bool shouldEncounterOnStep( float32 encounterRate, uint32 stepCount );

    /** @brief 타일 스텝 이동과 워프·조우·상호작용 요청을 만듭니다. */
    class SW_GF_API PlayerController
    {
    public:
        /** @brief 타일 (1,1), 입력 허용으로 시작합니다. */
        PlayerController();

        /** @brief 충돌·워프 조회에 쓸 타일맵을 설정합니다. */
        void setTileMap( TileMap* pTileMap ) { _pTileMap = pTileMap; }
        /** @brief 이동/상호작용 액션을 읽을 ActionMap을 설정합니다. */
        void setActionMap( ActionMap* pActionMap ) { _pActionMap = pActionMap; }
        /**
         * @brief 조우 타일에서 전투가 날 확률을 설정합니다. **0 이면 나지 않습니다.**
         * @details 0 ~ 1 로 읽는다. 0.33 이면 세 걸음마다 한 번꼴이고, 1 이상이면 매 걸음이다.
         *          예전에는 0 이 "세 걸음마다" 였다 — 끄려고 부른 값이 켜는 값이었다.
         */
        void setEncounterRate( float32 rate ) { _encounterRate = rate; }
        /** @brief 타일 좌표를 설정합니다. */
        void setPosition( int32 x, int32 y );
        /** @brief 입력 허용 여부를 설정합니다. */
        void setInputEnabled( bool enabled ) { _bInputEnabled = enabled ? 1 : 0; }
        /** @brief 입력을 읽고 이동 FSM을 갱신합니다. */
        void update( float32 deltaTime, InputManager& input );

        /** @brief 현재 타일 X를 반환합니다. */
        int32 getTileX() const { return _tile._x; }
        /** @brief 현재 타일 Y를 반환합니다. */
        int32 getTileY() const { return _tile._y; }
        /** @brief 이동 FSM을 반환합니다. */
        const PlayerLocomotion& getLocomotion() const { return _loco; }
        /** @brief 이동 플래그를 소비하고 이전 값을 반환합니다. */
        bool consumeMovedFlag();
        /** @brief 대기 중인 워프 요청을 소비합니다. */
        bool consumeWarpRequest( string& outMapPath, int32& outSpawnX, int32& outSpawnY );
        /** @brief 대기 중인 조우 요청을 소비합니다. */
        bool consumeEncounterRequest();
        /** @brief 대기 중인 상호작용 요청을 소비합니다. */
        bool consumeInteractRequest();

        /** @brief 바라보는 방향의 타일 좌표를 채웁니다. */
        void getFacingTile( int32& outX, int32& outY ) const;

    private:
        /** @brief 한 칸 이동을 시도합니다. */
        bool tryStep( int32 deltaX, int32 deltaY );

    private:
        TileMap*               _pTileMap;
        ActionMap*             _pActionMap;
        string                 _pendingWarpMap;
        PlayerLocomotion       _loco;
        int2                   _tile;             ///< 현재 타일 좌표
        int2                   _pendingWarpSpawn; ///< 워프 후 놓일 타일 좌표
        uint32                 _encounterStepCounter;
        float32                _encounterRate;
        uint8                  _bMoved            : 1;
        uint8                  _bWarpPending      : 1;
        uint8                  _bEncounterPending : 1;
        uint8                  _bInteractPending  : 1;
        uint8                  _bInputEnabled     : 1;
        [[maybe_unused]] uint8 _reserved          : 3;
    };
} // namespace sw
