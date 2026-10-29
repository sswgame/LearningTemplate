/**
 * @file NetLineEmulation.h
 * @brief 게임 네트워크 세션의 회선 흉내(지연 · 손실 · 끊김)입니다(개발 도구 `DevTools` — 배포 빌드는 짓지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    class GameNetSession;
    class INetTransport;

    /**
     * @class NetLineEmulationSubsystem
     * @brief 세션의 샤드 전송 데코레이터(`GameNetSession::setTransportDecorator`)로 회선 흉내(`NetEmulationTransport`)를 겁니다. 개발 명령 `net.emulation` 이 조건을 바꾼다.
     * @details 시작 조건은 `-gv_netEmulation*` 낱값 또는 `-gv_netEmulationPreset` 이다. 조건이 꺼져 있으면 흉내는 패킷을 그대로 넘긴다 —
     *          실행 중에 조건을 바꿀 수 있게 늘 감싼다. 흉내는 샤드가 소유하고(소켓보다 먼저 사라진다), 이 서브시스템은 자기가 만든 것만 알아본다.
     *          세션에 의존한다 — 세션이 열기 전에 걸리고, 세션보다 먼저 내려가며 데코레이터를 푼다.
     */
    class SW_DEVTOOLS_API NetLineEmulationSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "NetLineEmulation";
        /** @brief 알아보는 샤드 수의 상한입니다(그 뒤 샤드는 감싸지 않는다). */
        static constexpr int32 kMaxShardCount = 64;

        explicit NetLineEmulationSubsystem( GameNetSession& session );
        ~NetLineEmulationSubsystem() override;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 샤드 @p shardIndex 의 흉내입니다. 이 서브시스템이 감싼 것이 아니면(세션을 먼저 열었다) nullptr 입니다. */
        NetEmulationTransport* find( GameNetSession& session, int32 shardIndex ) const;
        /** @brief 흉내(모든 샤드)와 연결들의 수치를 한 보고로 모읍니다(`net.emulation stats` 의 답). */
        NetEmulationReport makeReport( GameNetSession& session ) const;

    private:
        /** @brief 세션이 샤드마다 부른다 — 시작 조건을 읽어 흉내로 감싼다. */
        unique_ptr<INetTransport> decorate( int32 shardIndex, INetTransport& inner );

    private:
        GameNetSession*        _pSession;
        NetEmulationTransport* _arrIssued[kMaxShardCount]; ///< 샤드마다 마지막으로 만든 흉내(세션이 든 것과 같을 때만 쓴다 — 다시 열면 바뀐다)
    };
} // namespace sw
