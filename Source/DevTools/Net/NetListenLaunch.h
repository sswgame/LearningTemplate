/**
 * @file NetListenLaunch.h
 * @brief 게임 네트워크 세션의 리슨 포트 덮어쓰기(`-gv_netListenPort` · `-gv_netLoopbackOnly`)입니다(개발 도구 `DevTools` — 배포 빌드는 짓지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    struct GameNetSessionParams;

    class GameNetSession;

    enum class GameNetLaunch : uint8;

    /**
     * @class NetListenLaunchSubsystem
     * @brief 세션의 기동 덮어쓰기(`GameNetSession::setLaunchOverride`)로 `-gv_netListenPort=<포트>` 를 서버 역할로 엽니다(0=덮어쓰지 않음).
     * @details 리슨 호스트(App)는 이 길로만 열고, 전용 서버에서는 서버 설정(`ServerConfig::_gamePort`)보다 우선한다(에디터 멀티 프로세스 재생 · 스크립트의 서버 짝이 고른 포트).
     *          언리얼 `?listen` · `-port=` 의 자리다. 평문 리슨은 루프백 · 개발 빌드로만 남긴다(U4 — `GameNetSession::makeServerParams`) — 그래서 배포에는 이 길이 없다.
     *          세션에 의존한다 — 세션이 열기 전에 걸리고, 세션보다 먼저 내려가며 덮어쓰기를 푼다.
     */
    class SW_DEVTOOLS_API NetListenLaunchSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "NetListenLaunch";

        explicit NetListenLaunchSubsystem( GameNetSession& session );
        ~NetListenLaunchSubsystem() override;

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

    private:
        /** @brief 세션이 기동 때 묻는다 — 리슨 포트가 있으면 서버 인자를 채운다. 포트가 범위 밖이면 알리고 `Invalid` 다. */
        GameNetLaunch makeListenParams( GameNetSessionParams& outParams );

    private:
        GameNetSession* _pSession;
    };
} // namespace sw
