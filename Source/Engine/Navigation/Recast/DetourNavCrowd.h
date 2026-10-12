/**
 * @file DetourNavCrowd.h
 * @brief DetourCrowd 로 구현한 `INavCrowd` 입니다. 경로 통로(corridor) 따라가기, 모퉁이 미리 돌기, 샘플 회피(RVO 계열), 떨어지기를 한 번의 `update` 로 처리합니다.
 * @details 회피 품질 넷(Low, Medium, Good, High)은 Detour 의 적응 샘플 설정 넷이고 에이전트마다 고른다. 목적지를 내비메시에서 찾지 못한 요청은
 *          이 클래스가 `Failed` 로 기억한다(Detour 는 그런 요청 자체를 받지 않는다).
 *
 *          **오프 메시 링크.** 경로의 다음 모퉁이가 링크면 DetourCrowd 가 에이전트를 링크 위로 옮긴다(두 끝 사이를 시간으로 보간). 그동안 Detour 는
 *          어느 링크인지 알려 주지 않으므로, 이 클래스가 갱신 직전의 마지막 모퉁이 폴리곤으로 링크를 기억해 상태(`NavCrowdAgentState::_link`)로 낸다.
 *          건너는 동안 바깥이 알린 위치(`syncAgentPosition`)는 무시한다 — 링크 위 위치는 군중이 정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/INavMesh.h"

class dtCrowd;
class dtNavMesh;
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

        /** @brief @p navMesh 위에 에이전트 @p maxAgentCount 개를 담을 수 있는 군중을 만듭니다. */
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
        /** @brief 에이전트 하나가 건너는(또는 곧 건널) 오프 메시 링크입니다. */
        struct LinkTraversal
        {
            float3 _start{};
            float3 _end{};
            float3 _previousPosition{}; ///< 갱신 직전 위치(속도 계산)
            float3 _velocity{};
            uint32 _pendingPoly{ 0 }; ///< 갱신 직전 마지막 모퉁이의 링크 폴리곤(없으면 0)
            uint32 _linkPoly{ 0 };    ///< 건너는 링크 폴리곤(아니면 0)
            uint32 _linkID{ 0 };
        };

        bool isActiveAgent( NavCrowdAgentID agentID ) const;
        /** @brief 에이전트가 링크를 건너기 시작했으면 그 링크(폴리곤 · 번호 · 오른 끝)를 기록합니다. */
        void beginLinkTraversal( LinkTraversal& inoutTraversal ) const;

        dtCrowd*              _pCrowd;
        dtNavMeshQuery*       _pQuery;     ///< 군중 밖 질의(목적지 붙이기, 위치 맞추기)에 쓴다. 게임 스레드만 쓴다.
        const dtNavMesh*      _pNavMesh;   ///< 링크 찾기(읽기만)
        vector<uint8>         _listFailed; ///< 에이전트마다 기록한다. 마지막 요청의 목적지를 찾지 못했다.
        vector<float3>        _listTarget; ///< 에이전트마다 기록한다. 마지막으로 지정한 목적지다(붙이기 전의 값).
        vector<LinkTraversal> _listLink;   ///< 에이전트마다 기록한다.
        uint32                _maxAgentCount;
    };
} // namespace sw
