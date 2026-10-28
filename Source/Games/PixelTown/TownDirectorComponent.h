/**
 * @file TownDirectorComponent.h
 * @brief PixelTown 의 규칙을 돌리는 컴포넌트입니다. 주인공의 타일 걸음(오버월드 키트), 충돌, 주민과의 대화를 맡습니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 한 칸 걸음 · 바라보는 칸 · 상호작용 요청은 키트(`OverworldTileMover`)가,
 *          걸을 수 있는 칸은 키트 타일맵(`TileMap`)이 정합니다. 이 컴포넌트는 그 둘을 씬의 데이터에 잇습니다.
 *
 *          - **맵은 씬의 렌더러가 읽은 것 그대로다.** 땅(`_ground`)과 소품(`_props`) 오브젝트의 `TileMapRendererComponent` 가 읽은 맵과 타일셋을 받아
 *            걷는 맵을 만든다. 막히는 칸의 기준은 타일셋 브러시의 `solid`(물 · 집 · 나무) 하나이고, 그리는 맵과 걷는 맵이 갈라지지 않는다.
 *          - **주민은 대화 러너(`DialogueRunnerComponent`)를 단 오브젝트다.** 판을 열 때 씬의 러너마다 그 오브젝트가 선 칸을 막고 말 상대로 적는다.
 *          - **입력은 폰의 의도다.** 주인공(`_hero`)의 `PawnComponent` 가 낸 이동 축과 버튼(`Town.Interact`)만 읽고 입력 맵을 읽지 않는다.
 *          - **대화는 틱 뒤 게임 스레드에서 돈다.** 규칙 틱(PrePhysics, 워커)은 "말을 걸었다 · 다음 줄"을 쌓기만 하고, 러너 호출과 자막은 플러시(`onFlush`)에서 한다.
 *            대화 중에는 걸음 입력을 막는다.
 *
 *          모습(주인공 스프라이트의 자리와 애니메이션)은 뷰 컴포넌트(`TownWalkerViewComponent`)가 이 컴포넌트를 **읽기만** 해서 맞춘다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Utility/Grid/FacingDir.h"
#include "GameFramework/Kits/Feature/World/Overworld/OverworldTileMover.h"
#include "GameFramework/Kits/Feature/World/Overworld/TileMap.h"

namespace sw
{
    class Archive;
    class GameObjectManager;
    class PawnComponent;
    class TileMapRendererComponent;

    /**
     * @class TownDirectorComponent
     * @brief 마을 한 판을 진행합니다. 플레이가 시작되면 씬의 타일 레이어로 걷는 맵을 만들고, 주인공을 맵의 스폰 칸에 세웁니다.
     * @details 걸음 상태(칸, 바라보는 방향, 걷는 중)는 PROPERTY 가 아니라 `writeState` 로 게임 상태 스냅샷에 실려 핫 리로드와 세이브를 넘긴다.
     *          대화는 저장하지 않는다(복원하면 대화 밖에서 시작한다).
     */
    REFLECT( Category = "Overworld", DisplayName = "Town Director", Tooltip = "Runs the tile-step town: hero walking, tile collision and talking to villagers" )
    class TownDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        TownDirectorComponent();
        virtual ~TownDirectorComponent() override;

        /** @brief 걸음 상태(키트 이동기의 바이트)를 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        void writeState( Archive& outArchive ) const override;

        /** @brief 주인공 발의 월드 자리입니다. 걷는 동안은 두 칸 사이를 걸음 시간에 맞춰 잇습니다. 뷰는 PostUpdate 에서 이것만 읽습니다. */
        const float3& getHeroPosition() const { return _heroPosition; }
        /** @brief 주인공이 바라보는 방향입니다. */
        FacingDir getHeroFacing() const { return _mover.getLocomotion().getFacing(); }
        /** @brief 주인공이 지금 한 칸을 걷는 중이면 true 입니다. */
        bool  isHeroWalking() const { return _mover.getLocomotion().getState() == LocomotionState::Walk; }
        int32 getHeroTileX() const { return _mover.getTileX(); }
        int32 getHeroTileY() const { return _mover.getTileY(); }
        /** @brief 대화가 열려 있으면 true 입니다(줄을 보이는 중이거나 선택을 기다리는 중). */
        bool isTalking() const { return _talkTarget.isValid(); }
        /** @brief 이번 판에 보인 대화 줄 수입니다(시나리오 감시 값). */
        int32 getShownLineCount() const { return _shownLineCount; }
        /** @brief 이번 판에 끝까지 마친 대화 수입니다(시나리오 감시 값). */
        int32 getFinishedTalkCount() const { return _finishedTalkCount; }

    protected:
        /** @brief 땅 · 소품 레이어로 걷는 맵을 만들고 주민 칸을 막은 뒤 주인공을 스폰 칸에 세웁니다. 레이어나 주인공이 없으면 판을 열지 않습니다. */
        [[nodiscard]] bool startGame() override;
        /** @brief `writeState` 의 바이트를 읽어 한 번에 바꿉니다. 끝까지 맞지 않으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        bool               hasPendingSpawn() const override { return _pendingTalk != TalkRequest::None; }

    private:
        /** @brief 틱이 쌓아 두고 플러시가 처리하는 대화 요청입니다. */
        enum class TalkRequest : uint8
        {
            None = 0,
            Start,   ///< 바라보는 칸의 주민에게 말을 건다
            Advance, ///< 열린 대화의 다음 줄
        };
        /** @brief 말 상대 한 명 — 선 칸과 대화 러너를 단 오브젝트입니다. */
        struct Villager
        {
            int2             _tile{};
            GameObjectHandle _object{};
        };

    private:
        /** @brief 핸들의 오브젝트에 붙은 타일맵 렌더러입니다. 없으면 이름과 함께 알리고 nullptr 입니다. */
        const TileMapRendererComponent* findLayer( GameObjectHandle layer, const utf8* pRole ) const;
        /** @brief 씬의 대화 러너마다 그 오브젝트가 선 칸을 막고 말 상대로 적습니다. 맵 밖에 선 주민은 데이터 오류입니다. */
        [[nodiscard]] bool collectVillagers( GameObjectManager& manager );
        /** @brief 월드 자리가 든 칸입니다. */
        int2 computeTile( const float3& worldPosition ) const;
        /** @brief 칸 중심(발 높이)의 월드 자리입니다. */
        float3 computeTileCenter( int32 x, int32 y ) const;
        /** @brief 칸 @p tile 에 선 주민입니다. 없으면 nullptr 입니다. */
        const Villager* findVillager( const int2& tile ) const;
        /** @brief 주인공 폰입니다(`_hero` 오브젝트). 없으면 nullptr 입니다. */
        const PawnComponent* findHeroPawn() const;
        /** @brief 쌓인 대화 요청을 러너에 넘기고 러너 상태를 옮겨 적습니다. 게임 스레드에서만 부릅니다. */
        void processTalk( GameObjectManager& manager );
        /** @brief 걷는 중이면 두 칸 사이, 아니면 지금 칸 중심으로 주인공 자리를 맞춥니다. */
        void updateHeroPosition();

    private:
        PROPERTY( Category = "Town", DisplayName = "Ground", Tooltip = "Object with the ground TileMapRendererComponent (the walk grid and its spawn)" )
        GameObjectHandle _ground;
        PROPERTY( Category = "Town", DisplayName = "Props", Tooltip = "Object with the props TileMapRendererComponent (houses, trees); its solid tiles block. None = no props layer" )
        GameObjectHandle _props;
        PROPERTY( Category = "Town", DisplayName = "Hero", Tooltip = "Object with the hero PawnComponent" )
        GameObjectHandle _hero;
        PROPERTY( Category = "Town", DisplayName = "Interact Button", Tooltip = "Pawn button that talks and advances a talk (input map action name)" )
        hashed_string _interactButton;
        PROPERTY( Category = "Town", DisplayName = "Step Seconds", Min = 0.05, Max = 2.0, Units = s, Tooltip = "Time to walk one tile" )
        float32 _stepSeconds;

        TileMap            _tileMap;
        OverworldTileMover _mover;
        vector<Villager>   _listVillager;
        float3             _mapOrigin;     ///< 땅 레이어의 왼위 모서리(월드)
        float3             _heroPosition;  ///< 뷰가 읽는 주인공 발 자리
        int2               _stepFromTile;  ///< 걷는 중인 걸음의 출발 칸
        float32            _tileSize;      ///< 땅 타일셋의 한 칸 크기
        float32            _stepElapsed;   ///< 걷는 중인 걸음의 지난 시간
        GameObjectHandle   _talkTarget;    ///< 열린 대화의 주민(없으면 무효)
        GameObjectHandle   _pendingTarget; ///< `TalkRequest::Start` 의 주민
        int32              _shownLineCount;
        int32              _finishedTalkCount;
        TalkRequest        _pendingTalk;
    };
} // namespace sw
