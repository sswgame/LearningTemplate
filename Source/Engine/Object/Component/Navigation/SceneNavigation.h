/**
 * @file SceneNavigation.h
 * @brief 씬 하나의 내비게이션입니다. 에이전트 종류마다 내비메시와 군중을 관리하고, 기하 수집, 베이크(쿠킹본과 런타임), 장애물과 파괴에 따른 타일 재베이크, 에이전트 갱신을 맡습니다.
 * @details `GameObjectManager` 가 소유만 하고(`ScenePhysics` 와 같은 구조) 틱의 한 줄(`tick`. PrePhysics 와 DuringPhysics 틱 결과를 적용한 뒤, 애니메이션과
 *          물리 앞)만 정한다. 언리얼의 `UNavigationSystemV1`, `ARecastNavMesh`, `UCrowdManager` 를 씬 단위로 묶은 것에 해당한다.
 *
 *          **갱신 한 번**(게임 스레드): ① 플레이 중이면 표면과 에이전트가 쓰는 에이전트 종류의 내비메시를 준비한다(쿠킹본이 맞으면 읽고, 아니면 모든 타일을
 *          워커로 베이크한다). ② 장애물이 움직였으면 이전 위치와 새 위치를, 파괴나 수정자가 알린 영역(`invalidateArea`)을 타일로 바꿔 재베이크 큐에 넣는다. ③ 끝난 재베이크
 *          결과를 반영하고, 큐의 타일을 워커에 넘긴다(동시에 `_maxConcurrentTileBakeCount` 개까지, 같은 타일은 하나씩). ④ 에이전트 요청(목적지, 정지,
 *          순간이동)과 위치(컨트롤러가 옮긴 위치)를 군중에 넣고, ⑤ 군중을 진행하고, ⑥ 결과를 오브젝트와 컨트롤러에 쓴다.
 *
 *          **질의**(`findPath`, `findNearestPoint`, `raycast`)는 베이크한 내비메시에 대해 어느 틱에서든 부를 수 있다(스레드마다 질의 객체를 쓴다). 아직 베이크하지
 *          않은 종류면 실패한다. 베이크는 갱신이 한다(`ensureNavMesh` 는 게임 스레드에서 지금 베이크한다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/NavigationTypes.h"

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
     * @brief 그리는 메시나 강체만으로는 모양을 알 수 없는 오브젝트가 베이크 기하를 직접 내는 인터페이스입니다(파괴 오브젝트는 붙어 있는 조각만 냅니다).
     * @details 등록한 소스의 주인 오브젝트는 메시와 강체를 모으지 않고 이 소스만 쓴다. 모양이 바뀌면 소스가 `SceneNavigation::invalidateArea` 를 부른다.
     */
    class INavGeometrySource
    {
    public:
        virtual ~INavGeometrySource() = default;

        /** @brief 기하를 대신 내는 오브젝트를 반환합니다. */
        virtual const GameObject* getNavGeometryOwner() const = 0;
        /** @brief 현재 주인의 메시와 강체 대신 이 소스를 써야 하면 true 입니다(온전한 파괴 오브젝트는 false 이며, 보통 규칙으로 모읍니다). */
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
    /** @brief 디버그 그리기에서 무엇을 그릴지 정합니다(`gv_navDebugDraw` 의 비트). */
    struct NavDebugDrawFlag
    {
        static constexpr uint32 kMesh     = 1u << 0; ///< 폴리곤 테두리
        static constexpr uint32 kPath     = 1u << 1; ///< 에이전트 경로(모퉁이)
        static constexpr uint32 kVelocity = 1u << 2; ///< 에이전트의 실제 속도와 원하는 속도
        static constexpr uint32 kObstacle = 1u << 3; ///< 장애물 풋프린트
        /** @brief 걷는 면과 에이전트 경로를 씬의 메시로 그립니다(에디터 없는 게임 화면과 스크린샷에도 보입니다. 디버그 선은 에디터 뷰포트만 그립니다). */
        static constexpr uint32 kSolidView = 1u << 4;
        static constexpr uint32 kAll       = kMesh | kPath | kVelocity | kObstacle | kSolidView;
    };
} // namespace sw

namespace sw
{
    /** @class SceneNavigation @brief 자세한 내용은 파일 머리말을 참고하십시오. */
    class SW_API SceneNavigation
    {
    public:
        /** @brief 레지스트리에 없는 위치입니다. */
        static constexpr uint32 kNotRegistered = invalid_index::kUint32;

        SceneNavigation();
        ~SceneNavigation();

        SceneNavigation( const SceneNavigation& )            = delete;
        SceneNavigation& operator=( const SceneNavigation& ) = delete;

        /** @brief 기하를 모을 매니저를 정합니다(매니저가 만들 때 부릅니다). */
        void setObjectManager( GameObjectManager* pManager ) { _pManager = pManager; }
        /** @brief 베이크하던 작업을 기다리고 내비메시와 군중을 모두 해제합니다. 레지스트리는 그대로 둡니다(컴포넌트가 스스로 제거합니다). */
        void shutdown();

        /** @brief 씬 소스 경로입니다. 쿠킹본(`<씬>.navmesh`)을 찾는 데 씁니다(`Scene::instantiate`). */
        void          setSourcePath( string_view path ) { _sourcePath = string{ path }; }
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 쿠킹본을 직접 줍니다(테스트, 도구). 주면 리소스(`<씬>.navmesh`)를 찾지 않고 이것을 씁니다. */
        void setCookedAsset( shared_ptr<const NavMeshAsset> pAsset ) { _pCookedAsset = std::move( pAsset ); }
        /** @brief 설정 테이블을 덮어씁니다(테스트, 게임). 정하지 않으면 처음 쓸 때 `NavMeshSettings::kResourcePath` 를 읽습니다. 이미 베이크한 내비메시는 버립니다. */
        void setSettings( const NavMeshSettings& settings );
        /** @brief 설정 테이블을 반환합니다(없으면 읽습니다). */
        const NavMeshSettings& getSettings();

        /** @brief 갱신 한 번입니다. 순서는 파일 머리말에 있습니다. 게임 스레드에서 매니저가 부릅니다. */
        void tick( float32 deltaTime );

        void registerAgent( NavMeshAgentComponent* pAgent );
        void unregisterAgent( NavMeshAgentComponent* pAgent );
        /** @brief 에이전트를 군중에서 제거합니다(등록은 유지하므로 플레이를 다시 시작하면 다시 넣습니다). */
        void releaseAgent( NavMeshAgentComponent* pAgent );
        void registerSurface( NavMeshSurfaceComponent* pSurface );
        void unregisterSurface( NavMeshSurfaceComponent* pSurface );
        void registerObstacle( NavMeshObstacleComponent* pObstacle );
        void unregisterObstacle( NavMeshObstacleComponent* pObstacle );
        void registerGeometrySource( INavGeometrySource* pSource );
        void unregisterGeometrySource( INavGeometrySource* pSource );

        /**
         * @brief 월드 상자 영역의 타일을 다시 베이크하게 합니다(다음 갱신에 워커로). 아무 스레드에서나 불러도 됩니다.
         * @param bGeometryChanged 정적 기하가 바뀌었으면(파괴, 수정자) true 이며 기하를 다시 모읍니다. 장애물만 바뀐 것이면 false 입니다.
         */
        void invalidateArea( const AABB& bounds, bool bGeometryChanged );

        /**
         * @brief 에이전트 종류의 내비메시를 지금 준비합니다(쿠킹본 또는 베이크). 게임 스레드에서만 부릅니다. 모르는 종류이거나 기하가 없으면 nullptr 입니다.
         * @details 갱신이 플레이 시작에 부르는 것과 같다. 테스트와 도구가 갱신을 기다리지 않을 때 쓴다.
         */
        INavMesh* ensureNavMesh( const hashed_string& agentType );
        /** @brief 이미 준비한 내비메시를 반환합니다. 없으면 nullptr 입니다. */
        const INavMesh* findNavMesh( const hashed_string& agentType ) const;
        /** @brief 이미 준비한 군중을 반환합니다. 없으면 nullptr 입니다. */
        INavCrowd* findCrowd( const hashed_string& agentType ) const;
        /** @brief 마지막 전체 베이크(또는 쿠킹본 읽기)의 측정값입니다. */
        const NavMeshBakeStats* findBakeStats( const hashed_string& agentType ) const;
        /**
         * @brief 에이전트 종류의 내비메시를 준비하고(`ensureNavMesh`) 쿠킹본 항목(해시, 경계, 타일)을 채웁니다. 게임 스레드에서만 부르며, 쿠킹(`SceneNavigationCooker`)이 씁니다.
         * @return 준비하지 못했으면 false 입니다.
         */
        [[nodiscard]] bool makeCookedEntry( const hashed_string& agentType, NavMeshAssetEntry& outEntry );
        /** @brief 쿠킹본에서 읽었으면 true 입니다. */
        bool isLoadedFromCooked( const hashed_string& agentType ) const;

        /** @brief 경로를 찾을 때 에이전트 종류의 기본 필터(테이블의 영역 비용)를 씁니다. 찾는 범위는 종류의 몸 크기입니다. */
        NavPathStatus findPath( const hashed_string& agentType, const float3& start, const float3& end, NavPath& outPath ) const;
        /** @brief 경로를 찾을 때 쓸 필터를 직접 줍니다. */
        NavPathStatus      findPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter, NavPath& outPath ) const;
        [[nodiscard]] bool findNearestPoint( const hashed_string& agentType, const float3& position, NavLocation& outLocation ) const;
        [[nodiscard]] bool raycast( const hashed_string& agentType, const float3& start, const float3& end, NavRaycastHit& outHit ) const;

        /** @brief 베이크하는 중이거나 큐에 대기 중인 타일 수입니다. */
        uint32 getPendingTileCount() const;
        /** @brief 베이크하는 중이거나 큐에 대기 중인 타일을 모두 지금 베이크해 반영합니다(게임 스레드. 테스트와 쿠킹용). */
        void flushTileBakes();
        /** @brief 재베이크로 반영한 타일 수(누적, 진단용)입니다. */
        uint32 getRebakedTileCount() const { return _rebakedTileCount; }
        /** @brief 지금 등록된 에이전트 수입니다. */
        uint32 getAgentCount() const { return static_cast<uint32>( _listAgent.size() ); }

        /** @brief @p flags(`NavDebugDrawFlag`)에 해당하는 것을 선으로 그립니다. */
        void drawDebug( IPhysicsDebugRenderer& renderer, uint32 flags ) const;
        /**
         * @brief `NavDebugDrawFlag::kSolidView` 가 켜졌으면 걷는 면(영역 색, 폴리곤마다 명암)과 에이전트 경로 띠를 메시 하나로 만들어 씬의 오브젝트
         *        (`NavMeshDebugView`. 베이크에서 제외됩니다)에 설정하고, 꺼졌으면 그 오브젝트를 지웁니다. 게임 스레드에서, 씬 틱 밖에서 부릅니다(`EngineLoop`).
         */
        void updateDebugView( uint32 flags );

        /**
         * @brief 씬의 베이크 기하를 모읍니다. 표면의 규칙은 파일 `NavMeshSurfaceComponent.h` 머리말에 있습니다. 표면이 없으면 기본 규칙(메시와 Static 강체)을 씁니다.
         * @details 쿠킹(`SceneNavigationCooker`)과 런타임 베이크가 같은 함수를 거친다. 두 경로의 입력 해시가 같아야 하기 때문이다.
         */
        static void collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                     const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry );
        /** @brief 매니저에 놓인 표면 중 @p agentType 을 맡는 첫 표면을 반환합니다. */
        const NavMeshSurfaceComponent* findSurface( const hashed_string& agentType );
        /** @brief 등록된 기하 소스입니다(쿠킹용). */
        const vector<INavGeometrySource*>& getGeometrySources() const { return _listGeometrySource; }
        /** @brief 베이크 경계입니다. 표면의 상자(있으면)와 기하 경계가 겹치는 영역입니다. */
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
        vector<hashed_string>              _listFailedAgentType; ///< 모르는 종류다. 오류를 한 번만 알린다
        GameObjectManager*                 _pManager;
        GameObjectHandle                   _debugView; ///< `updateDebugView` 가 만든 오브젝트
        uint32                             _rebakedTileCount;
        mutable SpinLock                   _dirtyLock; ///< `invalidateArea` 가 아무 스레드에서 온다
        bool                               _bSettingsLoaded;
        bool                               _bGeometryDirty; ///< `_dirtyLock` 아래
        bool                               _bObstacleVolumeDirty;
    };
} // namespace sw
