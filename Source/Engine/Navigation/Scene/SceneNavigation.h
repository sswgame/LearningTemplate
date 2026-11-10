/**
 * @file SceneNavigation.h
 * @brief 씬 하나의 내비게이션 — 에이전트 종류마다 내비메시 · 군중, 기하 모으기, 베이크(쿠킹본 · 런타임), 장애물 · 파괴의 타일 재베이크, 에이전트 갱신입니다.
 * @details 씬 서비스(`ISceneService`)입니다. 이 .cpp 의 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생기고, 매니저는 `Navigation` 단계(PrePhysics ·
 *          DuringPhysics 틱 결과 적용 뒤, 애니메이션 · 물리 앞 — 에이전트가 캐릭터 컨트롤러에 넘긴 속도가 이번 물리에 든다)에서 `update` 만 부릅니다.
 *          얻기는 `manager.getSubsystem<SceneNavigation>()` 입니다. 언리얼 `UNavigationSystemV1` + `ARecastNavMesh` + `UCrowdManager` 를 씬 단위로 묶은 자리입니다.
 *
 *          **갱신 한 번**(게임 스레드, `Navigation` 단계): ⓪ 지난 프레임에 넘긴 비동기 경로 질의를 수확한다(결과가 칸에 든다 — 이것이 내비메시 쓰기 증표
 *          `NavMeshWriteAccess` 를 낸다) ① 플레이 중이면 표면 · 에이전트가 쓰는 에이전트 종류의 내비메시를 마련한다(쿠킹본이 맞으면 읽고, 아니면 모든 타일을
 *          워커로 베이크한다) ② 장애물이 움직였으면 옛 · 새 자리를, 파괴 · 수정자가 알린 자리를(`invalidateArea`) 타일로 바꿔 재베이크 줄에 넣는다 ③ 끝난 재베이크
 *          결과를 끼우고(증표가 있어야 한다), 줄의 타일을 워커에 넘긴다(동시에 `_maxConcurrentTileBakeCount` 개까지, 같은 타일은 하나씩) ④ 에이전트 요청(목적지 · 멈춤 ·
 *          순간이동)과 자리(컨트롤러가 옮긴 자리)를 군중에 넣고, 이번 프레임까지 쌓인 경로 요청을 워커에 넘긴다 ⑤ 군중 진행을 종류마다 워커 일로 넘긴다.
 *          ⑥ 결과를 오브젝트 · 컨트롤러에 쓰는 것은 `FlushPostAnimation` 단계 본문 앞의 `completeFrameJob` 이다(⑤ 를 기다린 뒤) — 그 사이 `Animation` 단계가
 *          군중과 겹쳐 돈다. 군중 일이 읽고 쓰는 것은 군중 자신과 내비메시(읽기)뿐이다(등록 줄의 접근 `Navigation` 쓰기). 군중 결과는 일 하나가 종류 하나를
 *          통째로 진행해 스레드 · 일의 수와 상관없이 같다(결정적).
 *
 *          **질의**: 대상 수에 비례해 도는 경로는 `requestPath` 로 줄에 세우고 다음 갱신 뒤에 `takePathResult` 로 받는다(⓪ 이 수확한다 — 요청한 틱이 PrePhysics 면 다음
 *          프레임, PostPhysics 면 그다음 프레임의 PostPhysics 틱부터 보인다). 한 번 묻는 질의(`findPath` · `findNearestPoint` · `raycast` — 감시 값 · 도구 · 이벤트 하나)는
 *          동기로 어느 틱에서든 부를 수 있다(스레드마다 질의 객체, 타일 교체는 ③ 에서만 — 틱과 겹치지 않는다). 아직 베이크하지 않은 종류면 실패한다 — 베이크는 갱신이
 *          한다(`ensureNavMesh` 는 게임 스레드에서 지금 베이크한다). 에이전트(`NavMeshAgentComponent`)의 경로는 군중이 자기 경로 줄(Detour `dtPathQueue`)로 ⑤ 안에서
 *          구한다 — 군중 진행이 워커로 가면서 그 경로 찾기도 게임 스레드를 떠났다.
 *
 *          이 클래스는 레지스트리 · 종류별 내비메시 마련(쿠킹본 · 전체 베이크) · 질의 · 갱신 순서만 갖고, 바뀌는 이유가 다른 일은 조각이 맡습니다.
 *          기하 모으기는 `NavGeometryCollector`, 장애물 · 알린 영역의 타일 재베이크는 `NavTileRebaker`(값 멤버), 에이전트와 군중 사이 옮기기는
 *          `NavAgentCrowdSync`, 디버그 그리기는 `NavDebugView`(값 멤버)입니다. 모두 비가상 직접 호출입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Navigation/PathRequestQueue.h"
#include "Engine/Navigation/Scene/NavDebugView.h"
#include "Engine/Navigation/Scene/NavGeometryCollector.h"
#include "Engine/Navigation/Scene/NavPathSolver.h"
#include "Engine/Navigation/Scene/NavTileRebaker.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    struct NavMeshRuntime;

    class GameObject;
    class GameObjectManager;
    class INavCrowd;
    class INavMeshTileStore;
    class IPhysicsDebugRenderer;
    class NavMeshAgentComponent;
    class NavMeshGeometry;
    class NavMeshLinkComponent;
    class NavMeshObstacleComponent;
    class NavMeshSurfaceComponent;
    class NavMeshWriteAccess;

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
    class SW_API SceneNavigation : public ISceneService
    {
    public:
        /** @brief 레지스트리에 없는 자리입니다. */
        static constexpr uint32 kNotRegistered = constant::invalid_index::kUint32;
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneNavigation";

        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p manager 의 기하를 모으는 내비게이션을 만듭니다(레지스트리가 매니저마다 부른다). */
        explicit SceneNavigation( GameObjectManager& manager );
        ~SceneNavigation() override;

        SceneNavigation( const SceneNavigation& )            = delete;
        SceneNavigation& operator=( const SceneNavigation& ) = delete;

        /** @brief 베이크하던 일을 기다리고 내비메시 · 군중을 모두 놓습니다. 레지스트리는 그대로입니다(컴포넌트가 스스로 뺀다). */
        void onSceneCleared() override;

        /** @brief 씬 소스 경로 — 쿠킹본(`<씬>.navmesh`)을 찾는 데 씁니다(`Scene::instantiate`). */
        void          setSourcePath( string_view path ) { _sourcePath = string{ path }; }
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 쿠킹본을 직접 줍니다(테스트 · 도구). 주면 리소스(`<씬>.navmesh`)를 찾지 않고 이것을 씁니다. */
        void setCookedAsset( shared_ptr<const NavMeshAsset> pAsset ) { _pCookedAsset = std::move( pAsset ); }
        /** @brief 설정 표를 덮습니다(테스트 · 게임). 정하지 않으면 처음 쓸 때 `EngineDefaultAssets::_navMeshSettings` 를 읽습니다. 이미 베이크한 내비메시는 버린다. */
        void setSettings( const NavMeshSettings& settings );
        /** @brief 설정 표입니다(없으면 읽는다). */
        const NavMeshSettings& getSettings();

        /** @brief 갱신 한 번 — 파일 머리말의 순서(⓪ ~ ⑤)입니다. 게임 스레드에서 매니저가 부릅니다. */
        void update( float32 deltaTime ) override;
        /** @brief 워커에 넘긴 군중 진행을 기다리고 결과(⑥)를 오브젝트 · 컨트롤러에 씁니다(`FlushPostAnimation` 단계 본문 앞, 게임 스레드). 넘긴 것이 없으면 할 일이 없습니다. */
        void completeFrameJob() override;
        /** @brief 씬 원본 경로를 받아 쿠킹한 내비메시(`<씬>.navmesh`)를 찾는 이름으로 둡니다(`setSourcePath`). */
        void onSceneLoading( string_view sourcePath ) override { setSourcePath( sourcePath ); }
        /** @brief `gv_navDebugDraw` 의 비트대로 디버그 뷰를 맞추고 선을 그립니다. */
        void drawDebug( DebugDrawQueue& queue ) override;

        // 컴포넌트 등록은 구독을 돌려준다. 컴포넌트가 들고 `onUnregister` 에서 놓으면 빠진다. nullptr 이거나 이미 등록했으면 빈 구독이다.
        [[nodiscard]] Subscription registerAgent( NavMeshAgentComponent* pAgent );
        /** @brief 에이전트를 군중에서 뺍니다(등록은 지킨다 — 플레이를 다시 시작하면 다시 든다). */
        void                       releaseAgent( NavMeshAgentComponent* pAgent );
        [[nodiscard]] Subscription registerSurface( NavMeshSurfaceComponent* pSurface );
        [[nodiscard]] Subscription registerObstacle( NavMeshObstacleComponent* pObstacle );
        /** @brief 기하 소스를 올리고 그 구독을 돌려줍니다. 구독이 사라지면 빠집니다. nullptr 이거나 이미 올라 있으면 빈 구독입니다. */
        [[nodiscard]] Subscription registerGeometrySource( INavGeometrySource* pSource );
        [[nodiscard]] Subscription registerLink( NavMeshLinkComponent* pLink );
        /** @brief 지금 자세의 번호가 @p linkID 인 링크 컴포넌트입니다(에이전트가 건너는 링크의 종류 찾기). 없으면 nullptr 입니다. 링크 수만큼 훑습니다. */
        const NavMeshLinkComponent* findLink( uint32 linkID ) const;

        /**
         * @brief 월드 상자 자리의 타일을 다시 베이크하게 합니다(다음 갱신에 워커로). 아무 스레드에서 불러도 됩니다.
         * @param bGeometryChanged 정적 기하가 바뀌었으면(파괴 · 수정자) true — 다시 모은다. 장애물만 바뀐 것이면 false.
         */
        void invalidateArea( const AABB& bounds, bool bGeometryChanged );

        /**
         * @brief 에이전트 종류의 내비메시를 지금 마련합니다(쿠킹본 또는 베이크). 게임 스레드에서만 부릅니다. 모르는 종류 · 기하 없음이면 nullptr 입니다.
         * @details 갱신이 플레이 시작에 부르는 것과 같습니다 — 테스트 · 도구가 갱신을 기다리지 않을 때 씁니다.
         */
        const INavMeshTileStore* ensureNavMesh( const hashed_string& agentType );
        /**
         * @brief 마련한 내비메시를 모두 버리고, 표면이 맡은 에이전트 종류(이미 마련했던 종류 포함)를 지금 다시 베이크합니다. 쿠킹본은 쓰지 않습니다. 게임 스레드에서만 부릅니다.
         * @details 편집기의 베이크 버튼(`navigation.bake`)이 레벨을 고친 뒤 부르는 함수입니다 — 플레이 전에도 됩니다. 군중에 있던 에이전트는 다음 갱신에 다시 들어갑니다.
         *          보기는 `updateDebugView` · `drawDebug`, 쿠킹본으로 쓰기는 `makeCookedEntry` 입니다.
         * @return 다시 마련한 내비메시 수입니다.
         */
        uint32 rebakeAllNavMeshes();
        /** @brief 지금 마련한 에이전트 종류를 @p outListAgentType 에 채웁니다(편집기 보기 목록). */
        void collectBakedAgentTypes( vector<hashed_string>& outListAgentType ) const;
        /** @brief 이미 마련한 내비메시입니다. 없으면 nullptr 입니다. */
        const INavMeshTileStore* findNavMesh( const hashed_string& agentType ) const;
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

        /**
         * @brief 비동기 경로 요청 — 에이전트 종류의 거름(넘길 때의 영역 비용)으로 @p start 에서 @p end 까지를 줄에 세웁니다. 아무 스레드(병렬 틱)에서 부릅니다.
         * @details 다음 갱신이 워커에 넘기고 그다음 갱신(⓪)이 수확한다. 요청자는 핸들을 들고 `takePathResult` 로 받거나 `cancelPathRequest` 로 버린다 — 요청자가 사라지면
         *          버린다(언리얼 `AbortAsyncFindPathRequest`). 씬을 비우면 모든 요청이 사라진다(옛 핸들은 `Unknown`). 아직 베이크하지 않은 종류면 결과는 `Failed` 경로다.
         */
        [[nodiscard]] PathRequestHandle requestPath( const hashed_string& agentType, const float3& start, const float3& end );
        /** @brief 비동기 경로 요청 — 거름을 줍니다(이 요청의 사본으로 든다). */
        [[nodiscard]] PathRequestHandle requestPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter );
        /** @brief 요청을 버립니다(아무 스레드). 워커가 구하는 중이면 그 결과는 수확에서 버린다. */
        void cancelPathRequest( const PathRequestHandle& handle );
        /** @brief 결과가 있으면 @p outPath 로 옮기고 핸들을 끝냅니다(`Ready`). 아직이면 `Pending`, 끝난 · 버린 핸들이면 `Unknown` 입니다(아무 스레드). */
        [[nodiscard]] PathRequestState takePathResult( const PathRequestHandle& handle, NavPath& outPath );
        /** @brief 줄에 선 · 구하는 중인 경로 요청 수입니다(게임 스레드 — 진단 · 감시 값). */
        uint32 getPendingPathRequestCount() const { return _pathQueue.getPendingCount(); }
        /** @brief 수확한 경로 요청 수(누적, 진단)입니다. */
        uint64 getCompletedPathRequestCount() const { return _pathQueue.getCompletedCount(); }

        /** @brief 경로 — 에이전트 종류의 기본 거름(표의 영역 비용)입니다. 찾는 범위는 종류의 몸 크기입니다. 동기 질의 — 대상 수만큼 매 프레임 묻는 곳은 `requestPath` 를 쓴다. */
        NavPathStatus findPath( const hashed_string& agentType, const float3& start, const float3& end, NavPath& outPath ) const;
        /** @brief 경로 — 거름을 줍니다. */
        NavPathStatus      findPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter, NavPath& outPath ) const;
        [[nodiscard]] bool findNearestPoint( const hashed_string& agentType, const float3& position, NavLocation& outLocation ) const;
        [[nodiscard]] bool raycast( const hashed_string& agentType, const float3& start, const float3& end, NavRaycastHit& outHit ) const;
        /**
         * @brief 에이전트 종류 @p agentType 이 영역 @p areaName 을 지나는 비용 배율 @p cost 와 통행(@p bAllowed)을 정합니다(Recast 영역 비용 · 제외 비트).
         * @details 지역 프로필의 내비 비용 모듈(GameFramework `NavArea`)이 부른다 — 일반 NPC 는 용암을 돌아가고 화염 정령은 지난다(행위자 종류 = 에이전트 종류).
         *          이미 마련한 내비메시의 거름 · 군중에 바로 들고, 씬을 비울 때까지 기억해 그 뒤 마련하는 내비메시에도 든다. 같은 종류 · 영역을 다시 정하면 나중 값이다.
         *          언리얼 `UNavArea` 의 기본 비용과 `FNavigationQueryFilter` 의 영역 제외 · 유니티 `NavMesh.SetAreaCost` 의 자리다(기억).
         * @return 모르는 종류 · 영역이면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool setAgentAreaCost( const hashed_string& agentType, const hashed_string& areaName, float32 cost, bool bAllowed );
        /** @brief 에이전트 종류의 영역 비용 · 통행입니다(`setAgentAreaCost` 를 반영한 값, 진단 · 감시 값). 설정을 아직 읽지 않았거나 모르는 종류 · 영역이면 false 입니다. */
        [[nodiscard]] bool findAgentAreaCost( const hashed_string& agentType, const hashed_string& areaName, float32& outCost, bool& outbAllowed ) const;

        /** @brief 베이크하는 중이거나 줄에 선 타일 수입니다. */
        uint32 getPendingTileCount() const;
        /**
         * @brief 기하의 메시를 아직 읽는 중이라 내비메시 마련을 미룬 메시 수입니다(0 이면 기다리는 마련이 없다).
         * @details 자동화의 "에셋 일 0"(`AutomationCaptureUtil::countPendingAssetWork`)이 센다 — 메시가 도착한 뒤 내비메시가 서고 에이전트가 움직이기 전에 찍지 않게.
         */
        uint32 getWaitingMeshCount() const { return _waitingMeshCount; }
        /** @brief 베이크하는 중 · 줄에 선 타일을 모두 지금 베이크하고 끼웁니다(게임 스레드 — 테스트 · 쿠킹). */
        void flushTileBakes();
        /** @brief 재베이크로 끼운 타일 수(누적, 진단)입니다. */
        uint32 getRebakedTileCount() const { return _tileRebaker.getRebakedTileCount(); }
        /** @brief 지금 등록된 에이전트 수입니다. */
        uint32 getAgentCount() const { return static_cast<uint32>( _listAgent.size() ); }

        /** @brief @p flags(`NavDebugDrawFlag`)의 것을 선으로 냅니다. */
        void drawDebug( IPhysicsDebugRenderer& renderer, uint32 flags ) const;
        /**
         * @brief `NavDebugDrawFlag::kSolidView` 가 켜졌으면 걷는 면(영역 색 · 폴리곤마다 명암)과 에이전트 경로 띠를 메시 하나로 지어 씬의 오브젝트
         *        (`NavMeshDebugView` — 베이크에서 빠진다)에 걸고, 꺼졌으면 그 오브젝트를 지웁니다. 게임 스레드, 씬 틱 밖에서 부릅니다(`Engine`).
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
        const vector<INavGeometrySource*>& getGeometrySources() const { return _listGeometrySource.getItems(); }
        /** @brief 베이크 경계 — 표면의 상자(있으면)와 기하 경계의 겹침입니다. */
        static AABB computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry );

    private:
        friend class NavPathSolver; // 넘기기 직전 에이전트 종류를 내비메시로 푼다(`findRuntime`)

        /** @brief 영역 비용 덮어쓰기 하나입니다(`setAgentAreaCost`). */
        struct AreaCostOverride
        {
            hashed_string _agentType{};
            uint8         _areaIndex{ 0 };
            uint8         _bAllowed{ SW_TRUE };
            float32       _cost{ 1.0f };
        };

        /** @brief 덮어쓰기를 거름 @p inoutFilter 에 얹습니다(종류 @p agentType 의 것만). */
        void            applyAreaCostOverrides( const hashed_string& agentType, NavQueryFilter& inoutFilter ) const;
        NavMeshRuntime* findRuntime( const hashed_string& agentType ) const;
        /**
         * @brief 종류의 내비메시를 마련합니다(쿠킹본 또는 베이크). 모르는 종류 · 마련 실패면 nullptr 입니다.
         * @details @p arrival 이 `Defer` 이고 기하의 메시를 아직 읽는 중이면 마련하지 않고 nullptr 을 돌려준다 — `_waitingMeshCount` 가 그 수를 들고, 다음 갱신이 다시 마련한다.
         */
        NavMeshRuntime* createRuntime( const hashed_string& agentType, NavMeshArrival arrival );
        /** @brief 마련한 내비메시가 있으면 그것을, 없으면 `createRuntime` 입니다. */
        NavMeshRuntime*        findOrCreateRuntime( const hashed_string& agentType, NavMeshArrival arrival );
        const NavAgentTypeDef* resolveAgentType( const hashed_string& agentType );
        /** @brief 쿠킹본이 맞으면 그것으로 채웁니다. */
        bool installCooked( NavMeshRuntime& runtime, const NavAgentTypeDef& agentType, uint64 inputHash );
        /** @brief 알린 영역을 재베이크 줄로 바꿉니다(`NavTileRebaker::processDirtyAreas` 에 이 씬의 입력을 넘긴다). 갱신은 `Defer`, 그 자리에서 끝내는 `flushTileBakes` 는 `Wait` 다. */
        void processDirtyAreas( NavMeshArrival arrival );
        /** @brief 내비메시를 읽는 워커가 없게 합니다 — 군중 진행을 기다리고 경로 질의를 수확한다(결과가 칸에 든다). 그 증표로 타일을 바꾼다. 게임 스레드에서만 부릅니다. */
        [[nodiscard]] NavMeshWriteAccess completeNavMeshReads();
        /** @brief ④ — 에이전트 요청 · 자리를 군중에 넣습니다(게임 스레드). */
        void syncAgentsIntoCrowds();
        /** @brief ⑤ — 에이전트가 있는 군중마다 진행을 워커 일로 넘깁니다(워커가 없으면 이 자리에서 진행한다). */
        void launchCrowdUpdates( float32 deltaTime );
        /** @brief 넘긴 군중 진행이 끝날 때까지 기다립니다(군중을 만지는 모든 게임 스레드 일의 앞 — 수확 · 놓기 · 거름 바꾸기). */
        void waitCrowdUpdates();
        /** @brief 막기 · 풀기가 바뀐 링크를 모든 내비메시에 넣습니다(타일 교체와 같은 시점 — 질의가 없는 동안). */
        void applyLinkStates( const NavMeshWriteAccess& access );
        // 등록 구독이 풀렸을 때 부르는 해제 — 종류마다 구독 목록 하나라 id 에 종류를 싣지 않는다. 구독은 컴포넌트 · 소스가 살아 있는 동안 풀린다(`onUnregister`).
        /** @brief 기하 소스를 순서를 지키며 뺍니다. */
        void releaseGeometrySource( INavGeometrySource* pSource );
        void unregisterAgent( NavMeshAgentComponent* pAgent );
        void unregisterSurface( NavMeshSurfaceComponent* pSurface );
        void unregisterObstacle( NavMeshObstacleComponent* pObstacle );
        void unregisterLink( NavMeshLinkComponent* pLink );

    private:
        NavMeshSettings                    _settings;
        string                             _sourcePath;
        shared_ptr<const NavMeshAsset>     _pCookedAsset;
        vector<unique_ptr<NavMeshRuntime>> _listRuntime;
        vector<NavMeshAgentComponent*>     _listAgent;
        vector<NavMeshSurfaceComponent*>   _listSurface;
        vector<NavMeshObstacleComponent*>  _listObstacle;
        vector<NavMeshLinkComponent*>      _listLink; ///< 링크 컴포넌트(등록 · 해제는 `_navIndex` 로 O(1)). 순회는 매 갱신 `applyLinkStates` 의 켜짐 표지 확인과 `findLink`(에이전트가 링크에 오른 첫 갱신) 뿐이다. 링크는 문 · 사다리 · 점프 지점이라 씬당 수십 ~ 수백이어서 선형으로 충분하다. 수천이 되면 껐다 켠 링크만 모으는 목록과 id 색인으로 바꾼다

        RegistrationList<INavGeometrySource> _listGeometrySource;   ///< 구독으로 올린다. 등록 순서(빼도 순서 유지) — 한 주인을 여러 소스가 맡으면 먼저 등록한 것이 이긴다. 순서가 뜻이라 순서를 버리는 `unordered_set` 으로 바꾸지 않는다(소스가 수천이 되면 순서 보존 색인이 필요하다)
        vector<hashed_string>                _listFailedAgentType;  ///< 모르는 종류 — 오류를 한 번만
        vector<AreaCostOverride>             _listAreaCostOverride; ///< 에이전트 종류별 영역 비용 · 통행 덮어쓰기(씬을 비울 때까지)
        NavTileRebaker                       _tileRebaker;
        NavDebugView                         _debugView;
        NavPathSolver                        _pathSolver;              ///< `_pathQueue` 의 풀이(이 내비게이션의 내비메시 표를 읽는다) — 줄보다 먼저 선언한다
        PathRequestQueue<NavPathSolver>      _pathQueue;               ///< 비동기 경로 요청(⓪ 수확 · ④ 넘기기)
        TaskStageHandle                      _crowdStage;              ///< ⑤ 에서 넘긴 군중 진행 일 전부(`completeFrameJob` · `waitCrowdUpdates` 가 기다린다)
        vector<SceneTransformWrite>          _listAgentTransformWrite; ///< ⑥ 에서 모은 부모 없는 에이전트의 자리 · 회전 쓰기(`applyTransformBatch` 한 번 — 할당을 재사용한다)
        vector<NavMeshAgentComponent*>       _listWrittenAgent;        ///< ⑥ 에서 결과를 쓴 에이전트(쓰기를 펼친 뒤 쓴 번호를 적는다)
        GameObjectManager*                   _pManager;
        uint32                               _waitingMeshCount; ///< 이번 갱신에 마련을 미룬(`NavMeshArrival::Defer`) 모으기들이 센, 아직 읽는 중인 메시 수. 갱신 첫머리에 0 으로 돌린다
        float32                              _crowdDeltaTime;   ///< ⑤ 에서 넘긴 진행의 시간 — ⑥ 의 몸 돌리기가 같은 값을 쓴다
        uint8                                _bSettingsLoaded      : 1;
        uint8                                _bIgnoreCooked        : 1; ///< 다시 베이크하는 동안 쿠킹본을 읽지 않는다(`rebakeAllNavMeshes`)
        uint8                                _bCrowdUpdateLaunched : 1; ///< ⑤ 가 진행을 넘겼고 아직 ⑥ 이 쓰지 않았다 — 그동안 군중을 만지면 단언한다
        uint8                                _bCrowdTasksInFlight  : 1; ///< ⑤ 가 워커 태스크를 냈다(워커가 없어 이 자리에서 진행했으면 기다릴 것이 없다)
        // 슬롯 색인(`_navIndex`)으로 O(1) 빼는 목록 넷의 등록 구독입니다. 풀리면 그 종류의 `unregister…` 가 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다.
        ItemSubscriptionList<NavMeshAgentComponent>    _agentSubscriptions{ ItemSubscriptionList<NavMeshAgentComponent>::ReleaseFunction::create<&SceneNavigation::unregisterAgent>( this ) };
        ItemSubscriptionList<NavMeshSurfaceComponent>  _surfaceSubscriptions{ ItemSubscriptionList<NavMeshSurfaceComponent>::ReleaseFunction::create<&SceneNavigation::unregisterSurface>( this ) };
        ItemSubscriptionList<NavMeshObstacleComponent> _obstacleSubscriptions{ ItemSubscriptionList<NavMeshObstacleComponent>::ReleaseFunction::create<&SceneNavigation::unregisterObstacle>( this ) };
        ItemSubscriptionList<NavMeshLinkComponent>     _linkSubscriptions{ ItemSubscriptionList<NavMeshLinkComponent>::ReleaseFunction::create<&SceneNavigation::unregisterLink>( this ) };
    };
} // namespace sw
