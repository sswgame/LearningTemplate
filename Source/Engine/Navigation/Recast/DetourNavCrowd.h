/**
 * @file DetourNavCrowd.h
 * @brief DetourCrowd 로 구현한 `INavCrowd` 입니다 — 경로 통로(corridor) 따라가기 · 모퉁이 미리 돌기 · 표본 회피(RVO 계열) · 떨어지기를 한 번의 `update` 로.
 * @details 회피 품질 넷(Low · Medium · Good · High)은 Detour 의 적응 표본 설정 넷이고 에이전트마다 고릅니다. 목적지를 내비메시에서 찾지 못한 요청은
 *          이 클래스가 `Failed` 로 기억합니다(Detour 는 요청 자체를 받지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Navigation/INavMesh.h"

class dtCrowd;
class dtNavMeshQuery;

namespace sw
{
    class RecastNavMesh;

    /** @class DetourNavCrowd @brief 파일 머리말 참고. */
    class DetourNavCrowd final : public INavCrowd
    {
    public:
        DetourNavCrowd();
        ~DetourNavCrowd() override;

        DetourNavCrowd( const DetourNavCrowd& )            = delete;
        DetourNavCrowd& operator=( const DetourNavCrowd& ) = delete;

        /** @brief @p navMesh 위에 @p maxAgentCount 자리의 군중을 만듭니다. */
        [[nodiscard]] bool initialize( RecastNavMesh& navMesh, uint32 maxAgentCount, float32 maxAgentRadius );
        void               shutdown();

        NavCrowdAgentID    addAgent( const float3& position, const NavCrowdAgentParams& params ) override;
        void               removeAgent( NavCrowdAgentID agentID ) override;
        void               updateAgentParams( NavCrowdAgentID agentID, const NavCrowdAgentParams& params ) override;
        bool               requestMoveTarget( NavCrowdAgentID agentID, const float3& target ) override;
        void               resetMoveTarget( NavCrowdAgentID agentID ) override;
        void               teleportAgent( NavCrowdAgentID agentID, const float3& position ) override;
        void               syncAgentPosition( NavCrowdAgentID agentID, const float3& position ) override;
        void               update( float32 deltaTime ) override;
        [[nodiscard]] bool findAgentState( NavCrowdAgentID agentID, NavCrowdAgentState& outState ) const override;
        void               setQueryFilter( const NavQueryFilter& filter ) override;
        uint32             getActiveAgentCount() const override;
        uint32             getMaxAgentCount() const override { return _maxAgentCount; }
        void               drawDebug( IPhysicsDebugRenderer& renderer, bool bPath, bool bVelocity ) const override;

    private:
        bool isActiveAgent( NavCrowdAgentID agentID ) const;

        dtCrowd*        _pCrowd;
        dtNavMeshQuery* _pQuery;     ///< 군중 밖 질의(목적지 붙이기 · 자리 맞추기) — 게임 스레드만
        vector<uint8>   _listFailed; ///< 에이전트마다 — 마지막 요청의 목적지를 찾지 못했다
        vector<float3>  _listTarget; ///< 에이전트마다 — 마지막으로 건 목적지(붙이기 전)
        uint32          _maxAgentCount;
    };
} // namespace sw
