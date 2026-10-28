/**
 * @file ShooterDirectorComponent.h
 * @brief Shooter3D 의 규칙을 돌리는 컴포넌트입니다. 페이싱 감독이 정한 스켈레톤 스폰, 쓰러뜨린 수, 막는 상자, 효과 풀(탄착, 총구 섬광, 탄도선), 로그를 맡습니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 무기 규칙(연사, 탄창, 재장전, 퍼짐, 반동)은 기반 코드(`GameFramework/Base/Character/Combat`)가,
 *          이동, 사격, 체력은 플레이어 컴포넌트(`ShooterPlayerComponent`)가, 적 하나의 움직임은 `ShooterEnemyComponent` 가 맡는다.
 *
 *          틱 규칙은 다음과 같다. 디렉터는 `TickGroup::PrePhysics` 에서 쓰러진 적을 세고, 시체를 정리하고, 이번 프레임의 적 위치와 플레이어 위치를 기록한다.
 *          플레이어와 적(`DuringPhysics`)은 그 기록을 **읽기만** 한다(목록은 `data()` 로 읽는다). 스폰, 효과음, 효과는 틱 안에서 할 수 없으므로 쌓아 두었다가
 *          `executeOrDeferPostTick` 한 번으로 틱이 끝난 뒤에 처리한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/AI/Director/AIDirector.h"
#include "GameFramework/Base/Character/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Base/Character/AI/SpawnDirector.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"

namespace sw
{
    /**
     * @struct ShooterArenaMath
     * @brief 아레나의 충돌 계산입니다. 씬 없이 쓸 수 있습니다(플레이어, 적, 스폰 위치).
     */
    struct ShooterArenaMath
    {
        /** @brief 원(XZ)을 상자 밖으로 밀어냅니다. 상자 위(높이 이상)는 막지 않습니다. 뛰어 오른 경우입니다. */
        static float3 resolveCircle( const vector<ShooterArenaBox>& listBox, const float3& position, float32 radius );
    };
} // namespace sw

namespace sw
{
    class Archive;
    class GameObjectManager;
    class MaterialInstance;
    class MeshComponent;
    class ShooterPlayerComponent;

    /** @brief 이번 프레임의 살아 있는 적 하나입니다. 디렉터가 `PrePhysics` 에서 기록하고 다른 그룹이 읽습니다. 히트박스는 발에서 `_height` 까지의 캡슐입니다. */
    struct ShooterEnemyView
    {
        GameObjectHandle _object{};
        float3           _position{}; ///< 발 위치.
        float32          _radius{ 0.45f };
        float32          _height{ 2.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterDirectorComponent
     * @brief 아레나 한 판을 진행합니다. 플레이가 시작되면 막는 상자를 모으고 페이싱 감독(`AIDirector`, 데이터 `_pacingProfile` 과 `_spawnTable`)을 시작합니다.
     * @details 감독은 쌓기, 절정, 쉼 단계를 돌며 적 스폰(예산), 무리(절정 진입), 탄 채우기(쉼 진입), 수리(예산)를 정한다. 이 컴포넌트는 그 사건을 스켈레톤,
     *          탄, 체력으로 바꾼다. 스켈레톤은 프리팹 `_enemyPrefab` 이고, 모습은 외형 프리셋(`_listEnemyPreset` 을 차례로 쓰고 정예는 `_eliteEnemyPreset`)과
     *          스폰 순번 시드로 정한다. 긴장도 신호는 받은 피해(`reportPlayerDamage`), 쓰러뜨린 적, 가까운 적 수, 탄 부족이다. 웨이브 번호는
     *          감독의 순환 수에 1 을 더한 값이다. 스폰한 적과 효과는 핸들로 보관하고, 상태를 저장하기 전에 정리한다(`despawnViews`). 감독 예산으로 스폰한 적은 같은 스폰 ID 로 다시 스폰한다.
     *          적의 AI 조종자는 적 폰의 자동 빙의가 만들고 폰과 함께 지워진다.
     *
     *          **자동 플레이는 AI 조종자의 빙의로 구현한다.** 자동 플레이 스위치(`gv_shooterAutoPlay`, 씬의 `_bAutoPlay`, 에디터 툴바)가 바뀌면 틱이 끝난 뒤 플러시에서 플레이어 폰을
     *          `ShooterAutoAimControllerComponent`(스폰한 오브젝트) 또는 플레이어 0 의 조종자(`ControlSystem::findOrCreatePlayerController`)에게 넘긴다.
     *          플레이어 몸에는 자동 플레이 분기가 없다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Director", Tooltip = "Runs the skeleton waves, kills, blockers, the effect pools and the runtime spawns" )
    class ShooterDirectorComponent : public GameDirectorComponent
    {
    public:
        REFLECT_BODY();

        ShooterDirectorComponent();
        virtual ~ShooterDirectorComponent() override;

        /** @brief 움직임 기록(`-gv_shooterMotionTrace`)을 파일로 쓰고 판을 닫습니다. */
        void onEndPlay() override;

        /** @brief 판의 진행(처치 수, 페이싱 감독 상태)을 씁니다. `ComponentStateStore::capture` 가 부릅니다. 적과 효과는 모습이라 정리합니다. */
        void writeState( Archive& outArchive ) const override;
        /** @brief 플레이어가 쓰러졌습니다. 적을 정리하고 감독을 처음부터 다시 돌립니다(틱이 끝난 뒤 게임 스레드에서 부릅니다). */
        void restartRound();
        /** @brief 플레이어가 맞았습니다. 감독의 긴장도 신호로 넣습니다(틱이 끝난 뒤 게임 스레드에서 부릅니다). */
        void reportPlayerDamage( float32 amount );
        /** @brief 탄착과 총구 섬광 구 하나를 풀에서 꺼내 보입니다(게임 스레드). */
        void spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime );
        /** @brief 탄도선 하나(@p from 에서 @p to 까지, 굵기 @p width 의 길쭉한 상자)를 풀에서 꺼내 보입니다(게임 스레드). 에디터 없이도 보입니다. */
        void spawnTracer( const float3& from, const float3& to, float32 width, const float4& color, float32 lifetime );

        // ---- 다른 컴포넌트가 읽는 것(PrePhysics 뒤의 그룹) ----
        const vector<ShooterArenaBox>&  getBoxes() const { return _listBox; }
        const vector<ShooterEnemyView>& getEnemyViews() const { return _listEnemyView; }
        /** @brief 이번 프레임의 플레이어 눈 위치입니다. */
        const float3& getPlayerEye() const { return _playerEye; }
        /** @brief 이번 프레임의 플레이어 발 위치입니다. 적이 이쪽으로 옵니다. */
        const float3&    getPlayerFeet() const { return _playerFeet; }
        bool             isPlayerAlive() const { return _bPlayerAlive == SW_TRUE; }
        GameObjectHandle getPlayer() const { return _player; }
        float32          getArenaHalfSize() const { return _arenaHalfSize; }
        /** @brief 현재 웨이브입니다. 감독이 쌓기 단계로 돌아온 횟수에 1 을 더한 값입니다. */
        uint32            getWave() const { return static_cast<uint32>( _director.getCycle() + 1 ); }
        const AIDirector& getPacingDirector() const { return _director; }
        uint32            getKillCount() const { return _killCount; }

    protected:
        /** @brief 막는 상자를 모으고 페이싱 감독을 시작합니다(데이터를 읽지 못하면 적이 오지 않을 뿐 판은 시작합니다). */
        [[nodiscard]] bool startGame() override;
        /** @brief 판의 진행(처치 수, 페이싱 감독 상태)을 읽습니다. 적은 정리하고, 감독 예산으로 스폰했던 적만 같은 스폰 ID 로 다시 스폰합니다. */
        [[nodiscard]] bool readState( Archive& archive ) override;
        void               onStateRestored( bool bRestored ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        void               onViewsDespawned() override;
        /** @brief 스폰 요청이 쌓였거나 자동 플레이 스위치와 플레이어 폰의 조종자가 어긋났으면 true 를 반환합니다. 베이스가 틱 끝에 플러시를 잡습니다. */
        bool hasPendingSpawn() const override;

    private:
        /** @brief 스폰할 적 하나입니다. 틱이 끝난 뒤 프리팹으로 스폰하며, 위치는 그때 스폰 위치 순번(`_slot`)으로 정합니다. */
        struct EnemyRequest
        {
            float32 _health{ 30.0f };
            float32 _speed{ 3.0f };
            uint32  _slot{ 0 };
            uint32  _spawnID{ 0 }; ///< 감독의 스폰 예산으로 스폰한 적의 ID(0은 무리나 정예이며 예산 밖이다).
            uint8   _bElite{ SW_FALSE };
        };

        /** @brief 스폰한 적 하나입니다. 쓰러지면 처치 수와 신호를 한 번 내고 감독에 스폰 ID 를 돌려줍니다. 시체 시간이 지나면 정리합니다. */
        struct EnemyRecord
        {
            GameObjectHandle _object{};
            uint32           _spawnID{ 0 };
            uint8            _bCounted{ SW_FALSE };
        };

        /** @brief 낼 효과 하나입니다. 틱이 끝난 뒤 풀에서 꺼냅니다. */
        struct EffectRequest
        {
            float4  _color{};
            float3  _position{};
            float32 _size{ 0.1f };
            float32 _lifetime{ 0.1f };
        };

        /** @brief 효과 풀 하나입니다(같은 프리팹을 숨겨 두고 꺼내 씁니다). */
        struct EffectPool
        {
            vector<GameObjectHandle> _listObject{};
            uint32                   _cursor{ 0 }; ///< 다음에 볼 위치. 모두 쓰고 있으면 이 위치를 다시 쓴다.
        };

    private:
        void collectBoxes();
        void startPacing();
        void applyDirectorEvents();
        void requestEnemies( int32 count, float32 healthScale, uint32 spawnID, bool bElite );
        void spawnEffectPools( GameObjectManager& manager );
        void spawnPool( GameObjectManager& manager, const string& prefab, int32 count, const utf8* pName, EffectPool& outPool );
        /** @brief 풀에서 숨어 있는 것 하나를 꺼냅니다. 모두 쓰고 있으면 순번 위치의 것을 다시 씁니다. 없으면 nullptr 을 반환합니다. */
        MeshComponent* acquirePooledMesh( GameObjectManager& manager, EffectPool& inoutPool, float32 lifetime );
        void           spawnEnemy( GameObjectManager& manager, const EnemyRequest& request );
        void           updateEnemies();
        void           updatePlayerView();
        /** @brief 플레이어 폰을 자동 플레이 스위치에 맞는 조종자(자동 조준 AI 또는 플레이어 0)에게 넘깁니다(게임 스레드, 틱 밖). */
        void syncAutoPlayPossession( GameObjectManager& manager );
        void clearEnemies();
        void logStatus( float32 deltaTime );
        /** @brief `-gv_shooterMotionTrace=<경로>`: 지난 프레임에 그려진 플레이어 몸, 본, 카메라, 적 하나의 위치를 CSV 한 줄로 쌓습니다. */
        void appendMotionTrace( float32 deltaTime );

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "Skeleton enemy (skeletal mesh, animator, appearance, ShooterEnemyComponent)" )
        string _enemyPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "Pooled sphere for hits and muzzle flashes" )
        string _effectPrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab", Tooltip = "Pooled box stretched along each shot" )
        string _tracerPrefab;
        PROPERTY( Category = "Enemies", DisplayName = "Enemy Presets", Tooltip = "Appearance presets the regular spawns cycle through (seeded by the spawn order)" )
        vector<string> _listEnemyPreset;
        PROPERTY( Category = "Enemies", DisplayName = "Elite Preset", Tooltip = "Appearance preset of the elite encounter" )
        string _eliteEnemyPreset;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the ShooterPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "Scene", DisplayName = "Spawn Points", Tooltip = "Enemies rise at these objects, in order" )
        vector<GameObjectHandle> _listSpawnPoint;
        PROPERTY( Category = "Arena", DisplayName = "Arena Half Size", Min = 1.0, Units = m )
        float32 _arenaHalfSize;
        PROPERTY( Category = "Pacing", AssetPath, DisplayName = "Pacing Profile", Tooltip = "AI director profile (*.director.xml): intensity, phases, encounter and reward pools" )
        string _pacingProfile;
        PROPERTY( Category = "Pacing", AssetPath, DisplayName = "Spawn Table", Tooltip = "Enemy spawn budget (*.spawns.xml) the director scales per phase" )
        string _spawnTable;
        PROPERTY( Category = "Pacing", DisplayName = "Pacing Seed", Tooltip = "Seed of the director; the same seed and inputs give the same pacing" )
        int32 _pacingSeed;
        PROPERTY( Category = "Arena", DisplayName = "Effect Pool Size", Tooltip = "Hit and flash spheres kept hidden and reused", Min = 1 )
        int32 _effectPoolSize;
        PROPERTY( Category = "Arena", DisplayName = "Tracer Pool Size", Tooltip = "Tracer boxes kept hidden and reused", Min = 1 )
        int32 _tracerPoolSize;
        PROPERTY( Category = "Debug", DisplayName = "Status Log Interval", Tooltip = "Seconds between status log lines", Min = 0.1, Units = s )
        float32 _statusLogInterval;
        PROPERTY( Category = "Pacing", DisplayName = "Near Enemy Distance", Tooltip = "Enemies inside this distance count as close to the player (pacing signal)", Min = 0.0, Units = m )
        float32 _nearEnemyDistance;
        PROPERTY( Category = "Pacing", DisplayName = "Repair Health Per Scale", Tooltip = "Health one unit of a repair reward restores", Min = 0.0 )
        float32 _repairHealthPerScale;

        vector<ShooterArenaBox>  _listBox;
        vector<ShooterEnemyView> _listEnemyView;
        AIDirectorProfile        _profile;
        SpawnTable               _table;
        AIDirector               _director;
        vector<AIDirectorEvent>  _listDirectorEvent;
        vector<EnemyRecord>      _listEnemy;
        EffectPool               _effectPool;
        EffectPool               _tracerPool;
        vector<EnemyRequest>     _listPendingEnemy;
        vector<EffectRequest>    _listPendingEffect;
        MaterialTintCache        _tintCache;          ///< 효과 색(같은 색은 나눠 쓴다).
        string                   _motionTrace;        ///< 움직임 기록 CSV(진단용이며 끝날 때 파일로 쓴다).
        GameObjectHandle         _autoPlayController; ///< 자동 플레이 AI 조종자 오브젝트(스폰한 것이며 정리할 목록에 들어 있다).
        float3                   _playerEye;
        float3                   _playerFeet;
        float32                  _statusTimer;
        float32                  _pendingHeal; ///< 감독의 수리 보상. 틱이 끝난 뒤 플레이어 체력에 반영한다.
        uint32                   _spawnCursor; ///< 다음 적의 스폰 위치 순번.
        uint32                   _killCount;
        uint32                   _traceFrame;       ///< 움직임 기록의 프레임 번호.
        int8                     _appliedAutoPlay;  ///< 플레이어 폰에 맞춰 둔 자동 플레이(-1 아직, 0 끔, 1 켬).
        uint8                    _bAmmoPending : 1; ///< 탄 보상. 틱이 끝난 뒤 플레이어 탄을 채운다.
        uint8                    _bPacingReady : 1; ///< 프로필과 스폰 테이블을 읽었다.
        uint8                    _bPlayerAlive : 1;
        uint8                    _reserved     : 5;
    };
} // namespace sw
