/**
 * @file SceneNavigation.h
 * @brief 씬 하나의 내비게이션 — 에이전트 종류마다 내비메시 · 군중, 기하 모으기, 베이크(쿠킹본 · 런타임), 장애물 · 파괴의 타일 재베이크, 에이전트 갱신입니다.
 * @details `GameObjectManager` 가 소유만 하고(`ScenePhysics` 와 같은 자리) 틱의 한 줄(`tick` — PrePhysics · DuringPhysics 틱 결과 적용 뒤, 애니메이션 ·
 *          물리 앞)만 정합니다. 언리얼 `UNavigationSystemV1` + `ARecastNavMesh` + `UCrowdManager` 를 씬 단위로 묶은 자리입니다.
 *
 *          **갱신 한 번**(게임 스레드): ① 플레이 중이면 표면 · 에이전트가 쓰는 에이전트 종류의 내비메시를 마련한다(쿠킹본이 맞으면 읽고, 아니면 모든 타일을
 *          워커로 베이크한다) ② 장애물이 움직였으면 옛 · 새 자리를, 파괴 · 수정자가 알린 자리를(`invalidateArea`) 타일로 바꿔 재베이크 줄에 넣는다 ③ 끝난 재베이크
 *          결과를 끼우고, 줄의 타일을 워커에 넘긴다(동시에 `_maxConcurrentTileBakeCount` 개까지, 같은 타일은 하나씩) ④ 에이전트 요청(목적지 · 멈춤 ·
 *          순간이동)과 자리(컨트롤러가 옮긴 자리)를 군중에 넣고 ⑤ 군중을 진행하고 ⑥ 결과를 오브젝트 · 컨트롤러에 쓴다.
 *
 *          **질의**(`findPath` · `findNearestPoint` · `raycast`)는 베이크한 내비메시에 대해 어느 틱에서든 부를 수 있습니다(스레드마다 질의 객체). 아직 베이크하지
 *          않은 종류면 실패합니다 — 베이크는 갱신이 합니다(`ensureNavMesh` 는 게임 스레드에서 지금 베이크한다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Physics/Collision/AABB.h"

namespace sw
{
    struct NavMeshRuntime;

    class GameObject;
    class GameObjectManager;
    class INavCrowd;
    class INavMesh;
    class IPhysicsDebugRenderer;
    class NavMeshAgentComponent;
    class NavMeshGeometry;
    class NavMeshObstacleComponent;
    class NavMeshSurfaceComponent;

    /**
     * @class INavGeometrySource
     * @brief 그리는 메시 · 강체만으로는 모양을 알 수 없는 오브젝트가 베이크 기하를 직접 냅니다(파괴 오브젝트 — 붙어 있는 조각만).
     * @details 등록한 소스의 주인 오브젝트는 메시 · 강체를 모으지 않고 이 소스만 씁니다. 모양이 바뀌면 소스가 `SceneNavigation::invalidateArea` 를 부릅니다.
     */
    class INavGeometrySource
    {
    public:
        virtual ~INavGeometrySource() = default;

        /** @brief 기하를 대신 내는 오브젝트입니다. */
        virtual const GameObject* getNavGeometryOwner() const = 0;
        /** @brief 지금 주인의 메시 · 강체 대신 이 소스를 써야 하면 true 입니다(온전한 파괴 오브젝트는 false — 보통 규칙으로 모은다). */
        virtual bool overridesOwnerGeometry() const = 0;
        /** @brief 월드 공간 기하를 @p outGeometry 에 더합니다(영역 @p area). */
        virtual void collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const = 0;

    protected:
        INavGeometrySource()                                       = default;
        INavGeometrySource( const INavGeometrySource& )            = default;
        INavGeometrySource& operator=( const INavGeometrySource& ) = default;
    };
} // namespace sw

namespace sw
{
    /** @brief 디버그 그리기에서 무엇을 그릴지입니다(`gv_navDebugDraw` 의 비트). */
    struct NavDebugDrawFlag
    {
        static constexpr uint32 kMesh     = 1u << 0; ///< 폴리곤 테두리
        static constexpr uint32 kPath     = 1u << 1; ///< 에이전트 경로(모퉁이)
        static constexpr uint32 kVelocity = 1u << 2; ///< 에이전트 실제 · 원한 속도
        static constexpr uint32 kObstacle = 1u << 3; ///< 장애물 발자국
        /** @brief 걷는 면 · 에이전트 경로를 씬의 메시로(편집기 없는 게임 화면 · 스크린샷에도 보인다 — 디버그 선은 편집기 뷰포트만 그린다). */
        static constexpr uint32 kSolidView = 1u << 4;
        static constexpr uint32 kAll       = kMesh | kPath | kVelocity | kObstacle | kSolidView;
    };
} // namespace sw

namespace sw
{
    /** @class SceneNavigation @brief 파일 머리말 참고. */
    class SW_API SceneNavigation
    {
    public:
        /** @brief 등록부에 없는 자리입니다. */
        static constexpr uint32 kNotRegistered = invalid_index::kUint32;

        SceneNavigation();
        ~SceneNavigation();

        SceneNavigation( const SceneNavigation& )            = delete;
        SceneNavigation& operator=( const SceneNavigation& ) = delete;

        /** @brief 기하를 모을 매니저를 정합니다(매니저가 만들 때). */
        void setObjectManager( GameObjectManager* pManager ) { _pManager = pManager; }
        /** @brief 베이크하던 일을 기다리고 내비메시 · 군중을 모두 놓습니다. 등록부는 그대로입니다(컴포넌트가 스스로 뺀다). */
        void shutdown();

        /** @brief 씬 소스 경로 — 쿠킹본(`<씬>.navmesh`)을 찾는 데 씁니다(`Scene::instantiate`). */
        void          setSourcePath( string_view path ) { _sourcePath = string{ path }; }
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 쿠킹본을 직접 줍니다(시험 · 도구). 주면 리소스(`<씬>.navmesh`)를 찾지 않고 이것을 씁니다. */
        void setCookedAsset( shared_ptr<const NavMeshAsset> pAsset ) { _pCookedAsset = std::move( pAsset ); }
        /** @brief 설정 표를 덮습니다(시험 · 게임). 정하지 않으면 처음 쓸 때 `NavMeshSettings::kResourcePath` 를 읽습니다. 이미 베이크한 내비메시는 버린다. */
        void setSettings( const NavMeshSettings& settings );
        /** @brief 설정 표입니다(없으면 읽는다). */
        const NavMeshSettings& getSettings();

        /** @brief 갱신 한 번 — 파일 머리말의 순서입니다. 게임 스레드에서 매니저가 부릅니다. */
        void tick( float32 deltaTime );

        void registerAgent( NavMeshAgentComponent* pAgent );
        void unregisterAgent( NavMeshAgentComponent* pAgent );
        /** @brief 에이전트를 군중에서 뺍니다(등록은 지킨다 — 플레이를 다시 시작하면 다시 든다). */
        void releaseAgent( NavMeshAgentComponent* pAgent );
        void registerSurface( NavMeshSurfaceComponent* pSurface );
        void unregisterSurface( NavMeshSurfaceComponent* pSurface );
        void registerObstacle( NavMeshObstacleComponent* pObstacle );
        void unregisterObstacle( NavMeshObstacleComponent* pObstacle );
        void registerGeometrySource( INavGeometrySource* pSource );
        void unregisterGeometrySource( INavGeometrySource* pSource );

        /**
         * @brief 월드 상자 자리의 타일을 다시 베이크하게 합니다(다음 갱신에 워커로). 아무 스레드에서 불러도 됩니다.
         * @param bGeometryChanged 정적 기하가 바뀌었으면(파괴 · 수정자) true — 다시 모은다. 장애물만 바뀐 것이면 false.
         */
        void invalidateArea( const AABB& bounds, bool bGeometryChanged );

        /**
         * @brief 에이전트 종류의 내비메시를 지금 마련합니다(쿠킹본 또는 베이크). 게임 스레드에서만 부릅니다. 모르는 종류 · 기하 없음이면 nullptr 입니다.
         * @details 갱신이 플레이 시작에 부르는 것과 같습니다 — 시험 · 도구가 갱신을 기다리지 않을 때 씁니다.
         */
        INavMesh* ensureNavMesh( const hashed_string& agentType );
        /** @brief 이미 마련한 내비메시입니다. 없으면 nullptr 입니다. */
        const INavMesh* findNavMesh( const hashed_string& agentType ) const;
        /** @brief 이미 마련한 군중입니다. 없으면 nullptr 입니다. */
        INavCrowd* findCrowd( const hashed_string& agentType ) const;
        /** @brief 마지막 전체 베이크(또는 쿠킹본 읽기)의 숫자입니다. */
        const NavMeshBakeStats* findBakeStats( const hashed_string& agentType ) const;
        /**
         * @brief 에이전트 종류의 내비메시를 마련하고(`ensureNavMesh`) 쿠킹본 항목(해시 · 경계 · 타일)을 채웁니다. 게임 스레드에서만 — 쿠킹(`SceneNavigationCooker`)이 씁니다.
         * @return 마련하지 못했으면 false 입니다.
         */
        [[nodiscard]] bool makeCookedEntry( const hashed_string& agentType, NavMeshAssetEntry& outEntry );
        /** @brief 쿠킹본에서 읽었으면 true 입니다. */
        bool isLoadedFromCooked( const hashed_string& agentType ) const;

        /** @brief 경로 — 에이전트 종류의 기본 거름(표의 영역 비용)입니다. 찾는 범위는 종류의 몸 크기입니다. */
        NavPathStatus findPath( const hashed_string& agentType, const float3& start, const float3& end, NavPath& outPath ) const;
        /** @brief 경로 — 거름을 줍니다. */
        NavPathStatus      findPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter, NavPath& outPath ) const;
        [[nodiscard]] bool findNearestPoint( const hashed_string& agentType, const float3& position, NavLocation& outLocation ) const;
        [[nodiscard]] bool raycast( const hashed_string& agentType, const float3& start, const float3& end, NavRaycastHit& outHit ) const;

        /** @brief 베이크하는 중이거나 줄에 선 타일 수입니다. */
        uint32 getPendingTileCount() const;
        /** @brief 베이크하는 중 · 줄에 선 타일을 모두 지금 베이크하고 끼웁니다(게임 스레드 — 시험 · 쿠킹). */
        void flushTileBakes();
        /** @brief 재베이크로 끼운 타일 수(누적, 진단)입니다. */
        uint32 getRebakedTileCount() const { return _rebakedTileCount; }
        /** @brief 지금 등록된 에이전트 수입니다. */
        uint32 getAgentCount() const { return static_cast<uint32>( _listAgent.size() ); }

        /** @brief @p flags(`NavDebugDrawFlag`)의 것을 선으로 냅니다. */
        void drawDebug( IPhysicsDebugRenderer& renderer, uint32 flags ) const;
        /**
         * @brief `NavDebugDrawFlag::kSolidView` 가 켜졌으면 걷는 면(영역 색 · 폴리곤마다 명암)과 에이전트 경로 띠를 메시 하나로 지어 씬의 오브젝트
         *        (`NavMeshDebugView` — 베이크에서 빠진다)에 걸고, 꺼졌으면 그 오브젝트를 지웁니다. 게임 스레드, 씬 틱 밖에서 부릅니다(`EngineLoop`).
         */
        void updateDebugView( uint32 flags );

        /**
         * @brief 씬의 베이크 기하를 모읍니다 — 표면의 규칙(파일 `NavMeshSurfaceComponent.h` 머리말). 표면이 없으면 기본 규칙(메시 + Static 강체)입니다.
         * @details 쿠킹(`SceneNavigationCooker`)과 런타임 베이크가 같은 함수를 지납니다 — 두 길의 입력 해시가 같다.
         */
        static void collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                     const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry );
        /** @brief 매니저에 놓인 표면 중 @p agentType 을 맡는 첫 표면입니다. */
        const NavMeshSurfaceComponent* findSurface( const hashed_string& agentType );
        /** @brief 등록된 기하 소스입니다(쿠킹). */
        const vector<INavGeometrySource*>& getGeometrySources() const { return _listGeometrySource; }
        /** @brief 베이크 경계 — 표면의 상자(있으면)와 기하 경계의 겹침입니다. */
        static AABB computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry );

    private:
        NavMeshRuntime*        findRuntime( const hashed_string& agentType ) const;
        NavMeshRuntime*        createRuntime( const hashed_string& agentType );
        const NavAgentTypeDef* resolveAgentType( const hashed_string& agentType );
        /** @brief 쿠킹본이 맞으면 그것으로 채웁니다. */
        bool installCooked( NavMeshRuntime& runtime, const NavAgentTypeDef& agentType, uint64 inputHash );
        void collectObstacleVolumes( vector<NavConvexVolume>& outListVolume ) const;
        void updateObstacles();
        void processDirtyAreas();
        void applyFinishedBakes();
        void launchTileBakes();
        void updateAgents( float32 deltaTime );
        void syncAgentIntoCrowd( NavMeshAgentComponent& agent, NavMeshRuntime& runtime );
        void writeAgentResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime );
        void waitForAllBakes();

        NavMeshSettings                    _settings;
        string                             _sourcePath;
        shared_ptr<const NavMeshAsset>     _pCookedAsset;
        vector<unique_ptr<NavMeshRuntime>> _listRuntime;
        vector<NavMeshAgentComponent*>     _listAgent;
        vector<NavMeshSurfaceComponent*>   _listSurface;
        vector<NavMeshObstacleComponent*>  _listObstacle;
        vector<INavGeometrySource*>        _listGeometrySource;
        vector<AABB>                       _listDirtyArea;       ///< `_dirtyLock` 아래
        vector<hashed_string>              _listFailedAgentType; ///< 모르는 종류 — 오류를 한 번만
        GameObjectManager*                 _pManager;
        GameObjectHandle                   _debugView; ///< `updateDebugView` 가 세운 오브젝트
        uint32                             _rebakedTileCount;
        mutable SpinLock                   _dirtyLock; ///< `invalidateArea` 가 아무 스레드에서 온다
        bool                               _bSettingsLoaded;
        bool                               _bGeometryDirty; ///< `_dirtyLock` 아래
        bool                               _bObstacleVolumeDirty;
    };
} // namespace sw
