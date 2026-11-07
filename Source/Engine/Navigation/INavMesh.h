/**
 * @file INavMesh.h
 * @brief 엔진 쪽 내비메시 인터페이스입니다. 수명과 군중(`INavCrowd`) 만들기를 다루고, 역할별 인터페이스(타일 · 질의 · 바꾸기 · 디버그)를 모두 상속합니다.
 * @details 라이브러리(Recast, Detour)는 `Navigation/Recast/` 안에만 있고 여기에는 그 타입이 하나도 없다.
 *          백엔드를 바꾸려면 이 인터페이스를 구현하는 두 클래스(내비메시와 군중)와 `NavMeshBackend::createNavMesh` 의 한 줄을 바꾼다.
 *
 *          **역할 나눔.** 물리의 `IPhysicsScene` 과 같은 모양이다. 구현 하나가 역할을 함께 구현하지만 한 역할만 쓰는 코드는 그 역할의 인터페이스만 받는다 — 역할마다 스레드 규칙이 달라서
 *          베이크 워커가 교체를 부르거나 질의 코드가 장애물을 고치는 모양을 형식으로 막는다.
 *
 *          | 역할 | 인터페이스 | 쓰는 곳 |
 *          |---|---|---|
 *          | 타일 격자 · 베이크 · 교체 | `INavMeshTileStore` | 쿠킹(`NavMeshAsset`), 재베이크(`NavTileBakeJob`), 편집기 베이크 명령 |
 *          | 질의 | `INavMeshQuery` | 경로 · 가까운 점 · 시선 |
 *          | 링크 막기 · 장애물 캐시 | `INavMeshDynamics` | 씬 내비게이션의 갱신 |
 *          | 디버그 그리기 | `INavMeshDebug` | 씬 내비게이션의 보기 |
 *          | 수명 · 군중 | `INavMesh` | `SceneNavigation` 의 에이전트 종류 줄(`NavMeshRuntime`) |
 *
 *          **타일.** 내비메시는 XZ 타일 격자다. 타일 하나는 백엔드가 아는 불투명 바이트(`NavTileData`)이고, 베이크(`bakeTile`)는 입력만 읽는
 *          순수 함수라 워커 여럿이 같이 돌 수 있다. 교체(`replaceTile`)는 게임 스레드에서, 질의가 없는 동안 한다. 쿠킹본(`.navmesh`)은 이 바이트를
 *          그대로 싣는다(`NavMeshAsset`).
 *
 *          **질의.** 질의는 const 이고 여러 스레드가 같이 호출해도 된다. 스레드마다 자기 질의 객체(스크래치 슬롯)를 쓴다. 다만 교체와
 *          겹치면 안 된다. 교체는 씬 틱 밖(컴포넌트 틱이 끝난 뒤)에서만 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Navigation/INavMeshDebug.h"
#include "Engine/Navigation/INavMeshDynamics.h"
#include "Engine/Navigation/INavMeshQuery.h"
#include "Engine/Navigation/INavMeshTileStore.h"
#include "Engine/Navigation/NavigationTypes.h"

namespace sw
{
    struct NavMeshSettings;
} // namespace sw

namespace sw
{
    /**
     * @class INavCrowd
     * @brief 내비메시 하나 위의 군중입니다(경로 따라가기, 이웃 회피, 떨어지기). 언리얼의 `UCrowdManager` 와 DetourCrowd 에 해당합니다.
     * @details 한 번에 한 스레드만 다룬다 — 요청 · 결과 읽기는 게임 스레드, `update` 는 워커 일(`NavMeshRuntime::updateCrowd`)이고 그동안 게임 스레드는 만지지 않는다. `update` 가 모든 에이전트의 경로, 회피, 이동을 한 번에 진행한다. 에이전트의 위치를 다른 것이
     *          움직이면(캐릭터 컨트롤러) 갱신 전에 `syncAgentPosition` 으로 알려, 군중이 그 위치에서 이어 가게 한다.
     */
    class INavCrowd
    {
    public:
        virtual ~INavCrowd() = default;

        /** @brief 에이전트를 추가합니다. 위치는 가장 가까운 내비메시 위로 붙습니다. 슬롯이 다 찼으면 `kInvalidAgentID` 를 반환합니다. */
        virtual NavCrowdAgentID addAgent( const float3& position, const NavCrowdAgentParams& params ) = 0;
        /** @brief 에이전트를 뺍니다. */
        virtual void removeAgent( NavCrowdAgentID agentID ) = 0;
        /** @brief 몸과 움직임 값을 바꿉니다. */
        virtual void updateAgentParams( NavCrowdAgentID agentID, const NavCrowdAgentParams& params ) = 0;
        /** @brief 목적지를 겁니다. 목적지를 내비메시에서 찾지 못하면 false 이고 상태가 `Failed` 입니다. */
        virtual bool requestMoveTarget( NavCrowdAgentID agentID, const float3& target ) = 0;
        /** @brief 목적지를 지우고 멈춥니다. */
        virtual void resetMoveTarget( NavCrowdAgentID agentID ) = 0;
        /** @brief 순간이동입니다. 경로를 버리고 새 위치에서 다시 시작합니다(목적지는 다시 지정해야 합니다). */
        virtual void teleportAgent( NavCrowdAgentID agentID, const float3& position ) = 0;
        /** @brief 바깥에서 옮긴 위치를 알립니다(경로는 유지합니다). 다음 `update` 가 그 위치에서 이어 갑니다. */
        virtual void syncAgentPosition( NavCrowdAgentID agentID, const float3& position ) = 0;
        /** @brief 군중을 @p deltaTime 만큼 진행합니다. */
        virtual void update( float32 deltaTime ) = 0;
        /** @brief 에이전트의 상태입니다. 없는 번호면 false 입니다. */
        [[nodiscard]] virtual bool findAgentState( NavCrowdAgentID agentID, NavCrowdAgentState& outState ) const = 0;
        /** @brief 군중의 질의 필터(영역 비용, 막을 영역)를 바꿉니다. 모든 에이전트가 같이 씁니다. */
        virtual void setQueryFilter( const NavQueryFilter& filter ) = 0;
        /** @brief 쓰는 중인 에이전트 수입니다. */
        virtual uint32 getActiveAgentCount() const = 0;
        /** @brief 담을 수 있는 에이전트 수입니다. */
        virtual uint32 getMaxAgentCount() const = 0;
        /** @brief 에이전트의 경로(모퉁이)와 속도를 선으로 내보냅니다. */
        virtual void drawDebug( IPhysicsDebugRenderer& renderer, bool bPath, bool bVelocity ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @class INavMesh @brief 에이전트 종류 하나의 내비메시입니다. 파일 머리말 참고. */
    class INavMesh : public INavMeshTileStore, public INavMeshQuery, public INavMeshDynamics, public INavMeshDebug
    {
    public:
        ~INavMesh() override = default;

        /**
         * @brief 빈 타일 격자를 @p bounds 위에 깝니다(타일은 아직 없다). 다시 부르면 모든 타일을 버리고 다시 깝니다.
         * @param areaTable 영역 이름 표(번호만 쓴다)
         */
        [[nodiscard]] virtual bool initialize( const NavAgentTypeDef& agentType, const NavMeshSettings& areaTable, const AABB& bounds ) = 0;
        /** @brief 모든 타일과 질의 객체를 놓습니다. */
        virtual void shutdown() = 0;

        /** @brief 이 내비메시 위의 군중을 만듭니다. */
        virtual unique_ptr<INavCrowd> createCrowd( uint32 maxAgentCount, float32 maxAgentRadius ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 쓰는 백엔드를 고르는 곳입니다. 바꾸려면 여기 한 줄을 바꿉니다. */
    struct SW_API NavMeshBackend
    {
        /** @brief 쿠킹본이 어느 백엔드의 바이트인지 적는 이름입니다(다른 백엔드의 쿠킹본은 읽지 않는다). */
        static const utf8* getBackendName();
        /** @brief 쿠킹본 바이트의 형식 버전입니다(백엔드 라이브러리 버전이 바뀌면 올립니다). */
        static uint32 getBackendFormatVersion();
        /** @brief 빈 내비메시를 만듭니다. */
        static unique_ptr<INavMesh> createNavMesh();
    };
} // namespace sw
