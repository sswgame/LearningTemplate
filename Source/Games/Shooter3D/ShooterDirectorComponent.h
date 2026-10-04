/**
 * @file ShooterDirectorComponent.h
 * @brief Shooter3D 의 규칙을 돌리는 컴포넌트 — 드론 웨이브 · 쓰러뜨린 수 · 막는 상자 · 탄착 효과 풀 · 로그, 그리고 드론 프리팹 스폰 지시입니다.
 *
 * @details 언리얼 GameMode/GameState 의 자리입니다. 씬에 하나 둡니다. 무기 규칙(연사 · 탄창 · 재장전 · 퍼짐 · 반동)은 기반(`GameFramework/Combat`)이,
 *          이동 · 사격 · 체력은 플레이어 컴포넌트(`ShooterPlayerComponent`)가, 드론 하나의 움직임은 `ShooterDroneComponent` 가 맡습니다.
 *
 *          틱 규칙: 디렉터는 `TickGroup::PrePhysics` 에서 쓰러진 드론을 걷고 이번 프레임의 드론 자리 · 플레이어 눈 자리를 적습니다. 플레이어 · 드론
 *          (`DuringPhysics`)은 그것을 **읽기만** 합니다(목록은 `data()` 로). 스폰 · 효과음 · 탄착 효과는 틱 안에서 할 수 없으므로 쌓아 두고
 *          `executeOrDeferPostTick` 한 번으로 틱 뒤에 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"

namespace sw
{
    /**
     * @struct ShooterArenaMath
     * @brief 아레나의 충돌 계산입니다 — 씬 없이 쓴다(플레이어 · 드론 · 스폰 자리).
     */
    struct ShooterArenaMath
    {
        /** @brief 원(XZ)을 상자 밖으로 밀어냅니다. 상자 위(높이 이상)는 막지 않는다 — 뛰어 오른 것. */
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

    /** @brief 이번 프레임의 드론 하나 — 디렉터가 `PrePhysics` 에서 적고 다른 그룹이 읽습니다. */
    struct ShooterDroneView
    {
        GameObjectHandle _object{};
        float3           _position{};
        float32          _radius{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterDirectorComponent
     * @brief 아레나 한 판입니다. 플레이가 시작되면 막는 상자를 모으고 웨이브를 기다립니다.
     * @details 판의 상태(웨이브 · 쓰러뜨린 수)는 핫 리로드에서 처음부터 다시 섭니다. 세운 드론 · 효과는 핸들로 들고 상태 저장 전에 걷습니다
     *          (`despawnRuntime`) — 남은 디렉터는 다음 틱에 효과 풀을 다시 세우고 웨이브를 기다린다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Director", Tooltip = "Runs the drone waves, kills, blockers, the effect pool and the runtime spawns" )
    class ShooterDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterDirectorComponent();
        virtual ~ShooterDirectorComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 세운 드론 · 효과를 모두 지웁니다(상태 저장 전). */
        void despawnRuntime();
        /** @brief 판의 진행(웨이브 · 처치 수)을 씁니다 — `ComponentStateStore::capture` 가 부릅니다. 드론 · 효과는 모습이라 걷고 다시 세운다. */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 진행을 되살립니다 — 다시 만든 디렉터는 웨이브 대기 뒤 그 웨이브를 새로 세웁니다.
         * @details 플레이 시작 전이면 들고 있다가 `onBeginPlay` 끝에 적용합니다. 읽지 못하면 알리고 웨이브 1 부터 시작합니다.
         */
        void restoreState( vector<uint8>&& bytes );
        /** @brief 플레이어가 쓰러졌다 — 드론을 걷고 웨이브 1 부터 다시 기다립니다(틱 뒤 게임 스레드에서 부른다). */
        void restartRound();
        /** @brief 탄착 · 터짐 효과 하나를 풀에서 꺼내 보입니다(게임 스레드). */
        void spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime );

        // ---- 다른 컴포넌트가 읽는 것(PrePhysics 뒤의 그룹) ----
        const vector<ShooterArenaBox>&  getBoxes() const { return _listBox; }
        const vector<ShooterDroneView>& getDroneViews() const { return _listDroneView; }
        /** @brief 이번 프레임의 플레이어 눈 자리입니다 — 드론이 이쪽으로 온다. */
        const float3&    getPlayerEye() const { return _playerEye; }
        GameObjectHandle getPlayer() const { return _player; }
        /** @brief 드론이 맞았을 때 입는 모습입니다(디렉터가 게임 스레드에서 만든다 — 아직 없으면 비어 있다). */
        const shared_ptr<MaterialInstance>& getDroneLook( bool bFlashing ) const;
        float32                             getArenaHalfSize() const { return _arenaHalfSize; }
        uint32                              getWave() const { return _wave; }
        uint32                              getKillCount() const { return _killCount; }
        /** @brief 조준 · 사격 · 이동도 AI 가 하면 true 입니다(`_bAutoPlay` 또는 `-gv_shooterAutoPlay=1`). */
        bool isAutoPlayOn() const;

        /** @brief 핸들의 오브젝트에 붙은 디렉터입니다. 없으면 nullptr 입니다. 매니저 조회는 잠그지 않습니다. */
        static const ShooterDirectorComponent* resolveDirector( const GameObjectManager& manager, GameObjectHandle director );

    private:
        /** @brief 세울 드론 하나 — 틱 뒤에 프리팹으로 선다. */
        struct DroneRequest
        {
            float3  _position{};
            float32 _health{ 30.0f };
            float32 _speed{ 3.0f };
            float32 _bobPhase{ 0.0f };
        };

        /** @brief 낼 효과 하나 — 틱 뒤에 풀에서 꺼낸다. */
        struct EffectRequest
        {
            float4  _color{};
            float3  _position{};
            float32 _size{ 0.1f };
            float32 _lifetime{ 0.1f };
        };

        /** @brief 효과 색 하나의 머티리얼 인스턴스입니다(같은 색은 나눠 쓴다 — 배치 키가 인스턴스다). */
        struct ColorLook
        {
            shared_ptr<MaterialInstance> _instance{};
            float4                       _color{};
        };

    private:
        /** @brief 들고 있던 복원 바이트를 적용합니다. */
        void                         applyPendingState();
        void                         collectBoxes();
        void                         requestWave();
        void                         scheduleFlush();
        void                         flushPending();
        void                         spawnEffectPool( GameObjectManager& manager );
        void                         spawnDrone( GameObjectManager& manager, const DroneRequest& request );
        void                         applyDroneLook( MeshComponent& mesh );
        shared_ptr<MaterialInstance> acquireColorLook( MeshComponent& mesh, const float4& color );
        void                         updateDrones();
        void                         updatePlayerView();
        void                         logStatus( float32 deltaTime );
        GameObjectManager*           getObjectManager() const;

    private:
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _dronePrefab;
        PROPERTY( Category = "Prefabs", AssetPath, AssetType = "Prefab" )
        string _effectPrefab;
        PROPERTY( Category = "Scene", DisplayName = "Player", Tooltip = "Object with the ShooterPlayerComponent" )
        GameObjectHandle _player;
        PROPERTY( Category = "Scene", DisplayName = "Spawn Points", Tooltip = "Drones appear at these objects, in order" )
        vector<GameObjectHandle> _listSpawnPoint;
        PROPERTY( Category = "Arena", DisplayName = "Arena Half Size", Min = 1.0, Meta = "Units=m" )
        float32 _arenaHalfSize;
        PROPERTY( Category = "Arena", DisplayName = "Drone Height", Tooltip = "Hover height of a fresh drone", Meta = "Units=m" )
        float32 _droneHeight;
        PROPERTY( Category = "Arena", DisplayName = "Wave Delay", Tooltip = "Seconds between a cleared wave and the next", Min = 0.0, Meta = "Units=s" )
        float32 _waveDelay;
        PROPERTY( Category = "Arena", DisplayName = "Effect Pool Size", Tooltip = "Hit and burst spheres kept hidden and reused", Min = 1 )
        int32 _effectPoolSize;
        PROPERTY( Category = "Look", DisplayName = "Drone Flash", Meta = "Color", Tooltip = "Drone tint while it flashes from a hit" )
        float4 _droneFlashTint;
        PROPERTY( Category = "Arena", DisplayName = "Auto Play", Tooltip = "Aim, shoot and move by AI (-gv_shooterAutoPlay=1 also turns it on)" )
        bool _bAutoPlay;

        vector<ShooterArenaBox>      _listBox;
        vector<ShooterDroneView>     _listDroneView;
        vector<GameObjectHandle>     _listDrone;
        vector<GameObjectHandle>     _listEffect;
        vector<DroneRequest>         _listPendingDrone;
        vector<EffectRequest>        _listPendingEffect;
        vector<const utf8*>          _listPendingSound; ///< 낼 효과음(틱 뒤 — 오디오는 게임 스레드에서)
        vector<ColorLook>            _listColorLook;
        vector<uint8>                _pendingStateBytes; ///< 플레이 시작 전에 받은 복원 바이트(`restoreState`)
        shared_ptr<MaterialInstance> _droneLook;
        shared_ptr<MaterialInstance> _droneFlashLook;
        float3                       _playerEye;
        float32                      _waveTimer; ///< 드론이 없을 때 다음 웨이브까지 남은 시간
        float32                      _statusTimer;
        uint32                       _wave;
        uint32                       _killCount;
        uint8                        _bStarted        : 1;
        uint8                        _bPoolSpawned    : 1; ///< 효과 풀이 서 있다(걷으면 다음 틱이 다시 세운다)
        uint8                        _bFlushScheduled : 1;
        uint8                        _bAmmoPending    : 1; ///< 새 웨이브 — 틱 뒤에 플레이어 탄을 채운다
        uint8                        _reserved        : 4;
    };
} // namespace sw
