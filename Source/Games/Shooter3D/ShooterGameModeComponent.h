/**
 * @file ShooterGameModeComponent.h
 * @brief Shooter3D 의 규칙을 돌리는 컴포넌트입니다. 페이싱 감독이 정한 스켈레톤 스폰, 쓰러뜨린 수, 막는 상자, 효과 풀(탄착, 총구 섬광, 탄도선), 로그를 맡습니다.
 * @details 언리얼의 GameMode/GameState 에 해당합니다. 씬에 하나만 둡니다. 무기 규칙(연사, 탄창, 재장전, 퍼짐, 반동)은 기반 코드(`GameFramework/Runtime/Character/Combat`)가,
 *          이동, 사격, 체력은 플레이어 컴포넌트(`ShooterPlayerComponent`)가, 적 하나의 움직임은 `ShooterEnemyComponent` 가 맡는다.
 *
 *          틱 규칙은 다음과 같다. 디렉터는 `TickGroup::PrePhysics` 에서 쓰러진 적을 세고, 시체를 정리하고, 이번 프레임의 적 위치와 플레이어 위치를 기록한다.
 *          플레이어와 적(`DuringPhysics`)은 그 기록을 **읽기만** 한다(목록은 `data()` 로 읽는다). 스폰, 효과음, 효과는 틱 안에서 할 수 없으므로 쌓아 두었다가
 *          `executeOrDeferPostTick` 한 번으로 틱이 끝난 뒤에 처리한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/AssetReloadWatch.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirector.h"
#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"
#include "GameFramework/Runtime/World/Lifetime/PrefabPool.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"

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
        GameObjectHandle  _object{};
        float3            _position{}; ///< 발 위치.
        float32           _radius{ 0.45f };
        float32           _height{ 2.0f };
        uint32            _netEntityID{ 0 };    ///< 복제 엔티티 번호(서버가 스폰마다 매긴다 — 씬 오브젝트 id 는 월드마다 다르다)
        uint32            _appearanceSeed{ 0 }; ///< 외형 씨앗 — 복제 모습이 같은 얼굴이 되게 함께 보낸다
        float32           _yaw{ 0.0f };
        ShooterEnemyPhase _phase{ ShooterEnemyPhase::Rising };
        uint8             _kindIndex{ 0 }; ///< 적 종류(`_listEnemyKind` 의 자리, `kEliteKindIndex` 는 정예)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 적 한 종류입니다 — 생물마다 스켈레톤 · 애니메이션 그래프 · 물리(맞음 상자) · 알림 표가 달라 프리팹이 종류의 정본이고, 외형 프리셋이 그 몸의 모습이다.
     * @details 언리얼 스폰 표의 행이 클래스(Blueprint)를 가리키는 것과 같다 — 프리팹 하나에 프리셋만 바꾸던 예전 모양은 사람형 해골 한 종류만 받았다.
     */
    REFLECT()
    struct ShooterEnemyKind
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Enemy prefab (skeletal mesh, animator, appearance, ragdoll hitboxes, ShooterEnemyComponent)" )
        AssetRef<PrefabAsset> _prefab{};
        PROPERTY( Required, Tooltip = "Appearance preset of the body (presets.xml)" )
        string _preset{};
    };
} // namespace sw

namespace sw
{
    /** @brief 네트 클라이언트가 스냅샷에서 받아 보간한 적 하나입니다(`ShooterNetPawnComponent::collectReplicatedEnemies`). */
    struct ShooterReplicatedEnemy
    {
        float3            _position{};
        float32           _yaw{ 0.0f };
        uint32            _netEntityID{ 0 };
        uint32            _appearanceSeed{ 0 };
        ShooterEnemyPhase _phase{ ShooterEnemyPhase::Rising };
        uint8             _kindIndex{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterGameModeComponent
     * @brief 아레나 한 판을 진행합니다. 플레이가 시작되면 막는 상자를 모으고 페이싱 감독(`AIDirector`, 데이터 `_pacingProfile` 과 `_spawnTable`)을 시작합니다.
     * @details 감독은 쌓기, 절정, 쉼 단계를 돌며 적 스폰(예산), 무리(절정 진입), 탄 채우기(쉼 진입), 수리(예산)를 정한다. 이 컴포넌트는 그 사건을 스켈레톤,
     *          탄, 체력으로 바꾼다. 적은 종류(`ShooterEnemyKind` — 프리팹 + 외형 프리셋)이고, 보통 적은 `_listEnemyKind` 를 차례로 쓰고 정예는 `_eliteEnemyKind` 이며 모습은 외형 프리셋과
     *          스폰 순번 시드로 정한다. 긴장도 신호는 받은 피해(`reportPlayerDamage`), 쓰러뜨린 적, 가까운 적 수, 탄 부족이다. 웨이브 번호는
     *          감독의 순환 수에 1 을 더한 값이다. 스폰한 적과 효과는 핸들로 보관하고, 상태를 저장하기 전에 정리한다(`despawnViews`). 감독 예산으로 스폰한 적은 같은 스폰 ID 로 다시 스폰한다.
     *          적의 AI 조종자는 적 폰의 자동 빙의가 만들고 폰과 함께 지워진다.
     *
     *          **자동 플레이는 AI 조종자의 빙의로 구현한다.** 자동 플레이 스위치(`gv_shooterAutoPlay`, 씬의 `_bAutoPlay`, 에디터 툴바)가 바뀌면 틱이 끝난 뒤 플러시에서 플레이어 폰을
     *          `ShooterAutoAimControllerComponent`(스폰한 오브젝트) 또는 플레이어 0 의 조종자(`ControlSystem::findOrCreatePlayerController`)에게 넘긴다.
     *          플레이어 몸에는 자동 플레이 분기가 없다.
     *
     *          **네트 클라이언트의 월드**(넷 링크 역할이 Client)는 판을 돌리지 않는다 — 페이싱 감독 · 적 규칙은 서버 몫이고, 스냅샷의 적을 복제 모습으로 세워(`ShooterEnemyComponent::launchReplica`)
     *          적 목록(`getEnemyViews`)을 그 자리로 채운다. 그래서 자동 플레이 AI · 사격 예측 · 근접 예측이 혼자 하는 게임과 같은 목록을 본다. 역할은 프로세스 구성(한 프로세스 재생 · 전용 서버 + 앱)을 가리지 않고 링크가 정한다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Game Mode", Tooltip = "Runs the skeleton waves, kills, blockers, the effect pools and the runtime spawns" )
    class ShooterGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        /** @brief `ShooterEnemyView::_kindIndex` 의 정예 표시입니다. */
        static constexpr uint8 kEliteKindIndex = 255;

        ShooterGameModeComponent();
        virtual ~ShooterGameModeComponent() override;

        /** @brief 판의 진행(처치 수, 페이싱 감독 상태)을 씁니다. `ComponentStateStore::capture` 가 부릅니다. 적과 효과는 모습이라 정리합니다. */
        void serializeState( StateIO& io ) override;
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
        /** @brief 스폰한 적 수입니다(쓰러져 정리를 기다리는 것 포함 — 진단 · 개발 기록이 읽는다). */
        uint32 getEnemyCount() const { return static_cast<uint32>( _listEnemy.size() ); }
        /** @brief 스폰한 적 @p index 의 오브젝트입니다(스폰 순서). */
        GameObjectHandle getEnemyObject( uint32 index ) const { return _listEnemy[index]._object; }
        float32          getArenaHalfSize() const { return _arenaHalfSize; }
        /** @brief 현재 웨이브입니다. 감독이 쌓기 단계로 돌아온 횟수에 1 을 더한 값입니다. */
        uint32            getWave() const { return static_cast<uint32>( _director.getCycle() + 1 ); }
        const AIDirector& getPacingDirector() const { return _director; }
        uint32            getKillCount() const { return _killCount; }
        /** @brief 효과 풀 둘이 지금까지 새로 만든 인스턴스 수입니다(재사용하면 늘지 않는다 — 감시 값). */
        uint32 getEffectCreatedCount() const { return _effectPool.getCreatedCount() + _tracerPool.getCreatedCount(); }

    protected:
        /** @brief 막는 상자를 모으고 페이싱 감독을 시작합니다(데이터를 읽지 못하면 적이 오지 않을 뿐 판은 시작합니다). */
        [[nodiscard]] bool startGame() override;
        void               onRegister( GameObjectManager& manager ) override;
        void               onUnregister( GameObjectManager& manager ) override;
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
            uint32           _netEntityID{ 0 };
            uint32           _appearanceSeed{ 0 };
            uint8            _kindIndex{ 0 };
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

    private:
        void collectBoxes();
        void startPacing();
        void applyDirectorEvents();
        /** @brief 페이싱 프로필이 다시 읽혔으면(핫 리로드) 판을 이어 새 프로필로 옮깁니다(`AIDirector::rebindProfile`). 구조가 바뀌었으면 이 판은 옛 프로필로 돈다. */
        void applyReloadedProfile();
        void requestEnemies( int32 count, float32 healthScale, uint32 spawnID, bool bElite );
        void spawnEffectPools( GameObjectManager& manager );
        /** @brief 풀에서 효과 하나를 꺼냅니다. 모두 쓰고 있으면 가장 먼저 꺼낸 것을 다시 씁니다. 프리팹을 읽지 못하면 nullptr 을 반환합니다. */
        MeshComponent* acquirePooledMesh( GameObjectManager& manager, PrefabPool& inoutPool, int32 poolSize, const utf8* pName, float32 lifetime );
        void           spawnEnemy( GameObjectManager& manager, const EnemyRequest& request );
        /** @brief 스냅샷의 적 하나를 복제 모습으로 세웁니다(네트 클라이언트, 틱 뒤). */
        void spawnReplicaEnemy( GameObjectManager& manager, const ShooterReplicatedEnemy& replica );
        void updateEnemies();
        /** @brief 받은 스냅샷에 맞춰 복제 모습을 옮기고 · 세울 것을 쌓고 · 사라진 것을 지우며 적 목록을 채웁니다(네트 클라이언트). */
        void updateReplicaEnemies();
        void updatePlayerView();
        /** @brief 플레이어 폰을 자동 플레이 스위치에 맞는 조종자(자동 조준 AI 또는 플레이어 0)에게 넘깁니다(게임 스레드, 틱 밖). */
        void syncAutoPlayPossession( GameObjectManager& manager );
        void clearEnemies();
        void logStatus( float32 deltaTime );

    private:
        PROPERTY( Category = "Prefabs", Tooltip = "Pooled sphere for hits and muzzle flashes" )
        AssetRef<PrefabAsset> _effectPrefab;
        PROPERTY( Category = "Prefabs", Tooltip = "Pooled box stretched along each shot" )
        AssetRef<PrefabAsset> _tracerPrefab;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Auto Play Controller", Tooltip = "AI controller (with its behavior tree) that takes the player pawn while auto play is on" )
        AssetRef<PrefabAsset> _autoPlayPrefab;
        PROPERTY( Category = "Enemies", DisplayName = "Enemies", Tooltip = "Enemy kinds the regular spawns cycle through (seeded by the spawn order)" )
        vector<ShooterEnemyKind> _listEnemyKind;
        PROPERTY( Category = "Enemies", DisplayName = "Elite Enemy", Tooltip = "Enemy kind of the elite encounter" )
        ShooterEnemyKind _eliteEnemyKind;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the ShooterPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "Scene", DisplayName = "Spawn Points", Tooltip = "Enemies rise at these objects, in order" )
        vector<GameObjectHandle> _listSpawnPoint;
        PROPERTY( Category = "Arena", DisplayName = "Arena Half Size", Min = 1.0, Units = m )
        float32 _arenaHalfSize;
        PROPERTY( Category = "Pacing", AssetPath, DisplayName = "Pacing Profile", Tooltip = "AI director profile (*.director.xml): intensity, phases, encounter and reward pools" )
        string _pacingProfile;
        PROPERTY( Category = "Pacing", DisplayName = "Spawn Table", Tooltip = "Enemy spawn budget (*.spawns.xml) the director scales per phase" )
        AssetRef<SpawnTable> _spawnTable;
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

        vector<ShooterArenaBox>             _listBox;
        vector<ShooterEnemyView>            _listEnemyView;
        shared_ptr<const AIDirectorProfile> _pProfile; ///< 페이싱 프로필(공유 캐시의 한 벌, 떼어 내는 종류 — 다시 읽으면 감독이 번호를 이어 새 객체로 옮긴다)
        shared_ptr<const SpawnTable>        _pTable;
        AIDirector                          _director;
        vector<AIDirectorEvent>             _listDirectorEvent;
        vector<EnemyRecord>                 _listEnemy;
        PrefabPool                          _effectPool;
        PrefabPool                          _tracerPool;
        vector<EnemyRequest>                _listPendingEnemy;
        vector<EffectRequest>               _listPendingEffect;
        vector<ShooterReplicatedEnemy>      _listReplicaSample;  ///< 이번 프레임에 받은 적(네트 클라이언트)
        vector<ShooterReplicatedEnemy>      _listPendingReplica; ///< 틱 뒤에 세울 복제 모습
        MaterialTintCache                   _tintCache;          ///< 효과 색(같은 색은 나눠 쓴다).
        GameObjectHandle                    _autoPlayController; ///< 자동 플레이 AI 조종자 오브젝트(스폰한 것이며 정리할 목록에 들어 있다).
        float3                              _playerEye;
        float3                              _playerFeet;
        float32                             _statusTimer;
        float32                             _pendingHeal; ///< 감독의 수리 보상. 틱이 끝난 뒤 플레이어 체력에 반영한다.
        uint32                              _spawnCursor; ///< 다음 적의 스폰 위치 순번.
        uint32                              _killCount;
        AssetReloadWatch<AIDirectorProfile> _profileReload;    ///< 프로필을 받을 때의 프로필 캐시 다시 읽기 횟수(바뀌면 판을 이어 새 프로필로 옮긴다)
        uint32                              _nextEnemyNetID;   ///< 다음 적의 복제 엔티티 번호
        int8                                _appliedAutoPlay;  ///< 플레이어 폰에 맞춰 둔 자동 플레이(-1 아직, 0 끔, 1 켬).
        uint8                               _bAmmoPending : 1; ///< 탄 보상. 틱이 끝난 뒤 플레이어 탄을 채운다.
        uint8                               _bPacingReady : 1; ///< 프로필과 스폰 테이블을 읽었다.
        uint8                               _bPlayerAlive : 1;
        uint8                               _bReplica     : 1; ///< 네트 클라이언트의 월드다(판을 돌리지 않고 스냅샷의 적을 그린다)
        uint8                               _reserved     : 4;
    };
} // namespace sw
