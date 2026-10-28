/**
 * @file OverworldTileMover.h
 * @brief 타일을 한 칸씩 걷는 폰 이동과 locomotion FSM 입니다. 입력이 아니라 조종자가 낸 의도(`ControlIntent`)를 받습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/FourCcUtil.h"

#include "GameFramework/Base/World/Encounter/StepEncounter.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Overworld/PlayerLocomotion.h"

namespace sw
{
    struct ControlIntent;

    class Archive;
    class TileMap;

    // ------------------------------------------------------------------------------
    // 1) OverworldTileMover. 의도를 받아 한 칸 이동하고, 워프, 조우, 상호작용 플래그를 세운다
    //    화면 전환 중에는 setInputEnabled(false) 로 입력을 막는다
    // ------------------------------------------------------------------------------
    /** @brief 이동 축 데드존, 걸음 시간, 조우율입니다. 버튼과 이동 액션 이름은 폰의 스키마(`PawnComponent`)가 정합니다. */
    struct OverworldTileMoverSettings
    {
        float32 _moveDeadZone{ 0.5f };   ///< 이보다 작게 기운 이동 축은 걸음으로 보지 않는다
        float32 _encounterRate{ 0.33f }; ///< 조우 칸 걸음의 조우율이다. 결정적 주기(`StepEncounterMode::Period`, `1/rate` 걸음마다)로 읽는다. 0 은 끈다
        float32 _stepSeconds{ 0.18f };   ///< 한 칸을 밟는 시간이다. 걷는 연출과 다음 입력 잠금이 같은 값을 쓴다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타일 스텝 이동을 하고 워프, 조우, 상호작용 요청을 만듭니다.
     * @details 폰 쪽 코드이다. InputMap 이나 InputManager 를 읽지 않고, 그 오브젝트의 폰 의도(`PawnComponent::getIntent`)를 받는다.
     *          플레이어, AI, 기록 재생이 같은 방식으로 걷는다.
     */
    class SW_KIT_API OverworldTileMover
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "OPLC" );
        static constexpr uint32 kStateVersion = 1;

        /** @brief 타일 (1,1), 입력 허용으로 시작합니다. */
        OverworldTileMover();

        /** @brief 충돌과 워프 조회에 쓸 타일맵을 설정합니다. */
        void setTileMap( TileMap* pTileMap ) { _pTileMap = pTileMap; }
        /**
         * @brief 조우 타일에서 전투가 날 확률을 설정합니다. **0 이면 나지 않습니다.**
         * @details 0 ~ 1 로 읽습니다. 0.33 이면 세 걸음마다 한 번꼴이고, 1 이상이면 매 걸음입니다.
         */
        void                              setEncounterRate( float32 rate ) { _settings._encounterRate = rate; }
        void                              setSettings( const OverworldTileMoverSettings& settings ) { _settings = settings; }
        const OverworldTileMoverSettings& getSettings() const { return _settings; }
        /**
         * @brief 이동 설정이 정하는 조우 규칙입니다(기반 `StepEncounterRule`). 오버월드는 난수 없는 주기 판정, 유예 0 입니다.
         * @details 확률 + 유예 판정(클래식 JRPG · 몬스터 수집)은 장르 키트가 지역 표와 자기 난수로 같은 `StepEncounterCounter` 를 돌린다.
         */
        static StepEncounterRule makeEncounterRule( const OverworldTileMoverSettings& settings );
        /** @brief 타일 좌표를 설정합니다. */
        void setPosition( int32 x, int32 y );
        /** @brief 입력 허용 여부를 설정합니다. */
        void setInputEnabled( bool bEnabled ) { _bInputEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        /**
         * @brief 의도를 읽고 이동 FSM 을 갱신합니다.
         * @param intent 이번 틱의 의도입니다. 이동 축(`_move`, y 가 위쪽 타일)을 데드존으로 걸러 네 방향 한 칸으로 읽습니다.
         * @param interactButton 상호작용 버튼의 슬롯(`PawnComponent::findButton`)입니다. 그 버튼이 발동하면 상호작용합니다. -1이면 버튼이 없습니다.
         */
        void update( float32 deltaTime, const ControlIntent& intent, int32 interactButton );

        /** @brief 현재 타일 X 를 반환합니다. */
        int32 getTileX() const { return _tile._x; }
        /** @brief 현재 타일 Y 를 반환합니다. */
        int32 getTileY() const { return _tile._y; }
        /** @brief 이동 FSM 을 반환합니다. */
        const PlayerLocomotion& getLocomotion() const { return _loco; }
        /** @brief 이동 플래그를 소비하고 이전 값을 반환합니다. */
        bool consumeMovedFlag();
        /** @brief 대기 중인 워프 요청을 소비합니다. */
        bool consumeWarpRequest( string& outMapPath, int32& outSpawnX, int32& outSpawnY );
        /** @brief 대기 중인 조우 요청을 소비합니다. 무엇을 만나는지는 게임이 장르 키트의 지역 테이블로 정하며, 지역은 존 id 와 태그로 구분합니다. */
        bool consumeEncounterRequest();
        /** @brief 대기 중인 상호작용 요청을 소비합니다. */
        bool consumeInteractRequest();

        /** @brief 바라보는 방향의 타일 좌표를 채웁니다. */
        void getFacingTile( int32& outX, int32& outY ) const;
        /**
         * @brief 타일, 이동 상태, 대기 중인 워프(맵과 위치), 조우 걸음 수, 대기 플래그, 입력 허용 여부를 씁니다.
         * @details 빌려 쓰는 타일맵과 설정은 싣지 않는다. 세이브도 이 바이트를 쓴다.
         *          게임 상태는 스냅샷 봉투(`GameInstance::saveStateToFile`)로만 저장하며, 플래그는 공유 상태가 싣는다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 한 칸 이동을 시도합니다. */
        [[nodiscard]] bool tryStep( int32 deltaX, int32 deltaY );

    private:
        TileMap*                   _pTileMap;
        string                     _pendingWarpMap;
        PlayerLocomotion           _loco;
        int2                       _tile;             ///< 현재 타일 좌표
        int2                       _pendingWarpSpawn; ///< 워프 후 배치될 타일 좌표
        OverworldTileMoverSettings _settings;
        StepEncounterCounter       _encounterSteps; ///< 조우 칸 걸음만 센다
        uint8                      _bMoved            : 1;
        uint8                      _bWarpPending      : 1;
        uint8                      _bEncounterPending : 1;
        uint8                      _bInteractPending  : 1;
        uint8                      _bInputEnabled     : 1;
        [[maybe_unused]] uint8     _reserved          : 3;
    };
} // namespace sw
