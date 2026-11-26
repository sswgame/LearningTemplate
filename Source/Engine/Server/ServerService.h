/**
 * @file ServerService.h
 * @brief 전용 서버 루프가 돌리는 서버 서비스의 창구(`IServerService`)와 그 문맥입니다. 모듈 서브시스템(호스트 `EngineSubsystemHost::kServer`)이 공개하고 서버가 범위에서 찾습니다.
 * @details - 서버 서비스는 게임 틱과 따로 서버 프로세스와 함께 사는 일입니다(온라인 서비스 호스트, 서버 등록). 언리얼 `UEngineSubsystem`(모듈이 올라오면 엔진이 만든다)에
 *            `FTickableGameObject`(루프가 부르는 갱신)를 더한 자리입니다.
 *          - 서비스는 모듈 서브시스템이 멤버로 들고 이 형식으로 공개합니다(`SW_MODULE_SUBSYSTEM( Engine, Process, X, IServerService )`). 서버는 `engine::findSubsystem<IServerService>()` 로 찾는다.
 *            범위 조회는 형식 하나에 객체 하나라 서비스는 서버에 하나다. 둘째가 필요하면 첫째가 그것을 들고 묶는다.
 *          - 수명: 모듈 서브시스템은 모듈을 올릴 때(엔진 기동) 서지만, 서비스는 서버가 설정 · 관측 · 게임을 세운 뒤 `startService` 로 시작하고 게임보다 먼저 `stopService` 로 멈춥니다.
 *            모듈을 내리면(리로드 · 종료) 그 모듈의 자식 범위가 걷히며 서브시스템의 `stopSubsystem` 이 서비스를 멈춥니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Server/ServerKitDirectory.h"

namespace sw
{
    struct ServerConfig;

    class MetricRegistry;
    class ServiceHealthRegistry;

    /**
     * @brief 서버 서비스가 시작할 때 받는 서버 프로세스의 문맥입니다. 포인터는 모두 빌려 쓰며 서비스보다 오래 삽니다.
     * @details 서버 서비스만 받는다(`IServerService::startService`). 클라이언트와 함께 짓는 게임 코드(네트워크 세션)는 이 서버 전용 형식을 모른다. 전용 서버가
     *          공유 형식인 `const ServerConfig` 만 호스트 범위에 빌려 건다(`engine::findSubsystem<const ServerConfig>()`, App · 테스트에는 없어 nullptr).
     */
    struct ServerServiceContext
    {
        string                 _serverConfigPath{};         ///< 서버 설정 파일 경로(서비스가 옆 파일을 찾을 때 쓴다)
        const ServerConfig*    _pServerConfig{ nullptr };   ///< 읽은 서버 설정(`Config/Server/<게임>.json`)
        MetricRegistry*        _pMetricRegistry{ nullptr }; ///< `/metrics` 로 나가는 지표 레지스트리
        ServiceHealthRegistry* _pHealthRegistry{ nullptr }; ///< `/healthz`, `/readyz` 가 읽는 상태 레지스트리
        ServerKitDirectory*    _pKitDirectory{ nullptr };   ///< 서비스가 세운 키트 범위를 게임 모듈에 공개하는 호스트 창구(호스트 소유라 리로드에 매달리지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerShutdownNotice
     * @brief 서버가 종료 드레인에 들어가며 서비스 · 키트에 알리는 것입니다(`IServerService::beginShutdown`, 온라인 키트의 `IOnlineServerKit::beginShutdown`).
     * @details 언리얼 `AGameModeBase::HandleDisconnect` · 유니티 Multiplay 의 "서버가 곧 내려간다" 통지처럼, 키트가 진행 중인 매치 · 거래를 마무리하고 클라이언트에 끊김 사유를
     *          알릴 기회다. 기다림은 `_graceSeconds` 안에서 `getOpenJobCount` 가 0 이 될 때까지다. 0 이면 알리기만 하고 바로 내려간다.
     */
    struct ServerShutdownNotice
    {
        ShutdownCause _cause{ ShutdownCause::None };
        uint32        _graceSeconds{ 0 }; ///< 접속이 비기를 기다릴 최대 시간(서버 설정 `_drainWaitSeconds`)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServerService
     * @brief 서버 루프가 돌리는 서비스 하나의 창구입니다. 모든 함수는 서버의 게임 스레드에서 불립니다.
     */
    class SW_API IServerService
    {
    public:
        IServerService()          = default;
        virtual ~IServerService() = default;

        IServerService( const IServerService& )            = delete;
        IServerService& operator=( const IServerService& ) = delete;

        /**
         * @brief 서비스를 시작합니다. 설정상 할 일이 없으면 아무것도 세우지 않고 성공입니다. 이미 돌고 있으면 아무것도 하지 않습니다.
         * @return 시작해야 하는데 실패하면 그 이유입니다. 만든 것은 정리했고 서버는 기동을 멈춥니다.
         */
        [[nodiscard]] virtual expected<void> startService( const ServerServiceContext& context ) = 0;
        /** @brief 빌린 것을 모두 돌려주고 멈춥니다. 두 번 불러도 됩니다. */
        virtual void stopService() = 0;
        /** @brief `startService` 가 성공한 뒤 아직 멈추지 않았으면 true 입니다. */
        virtual bool isServiceRunning() const = 0;
        /** @brief 서버 루프가 돌 때마다 불립니다. 게임 틱이 멈춰 있어도(Dev `pause`) 불립니다. @p nowMs 는 UTC 밀리초입니다. */
        virtual void update( UnixMilliseconds nowMs ) = 0;
        /** @brief 이 서비스가 받고 있는 연결 수입니다(지표 `server_service_connections`). */
        virtual int32 getConnectionCount() const = 0;
        /**
         * @brief 이 서비스가 끝내기를 기다려야 하는 진행 중인 일의 수입니다(매치 · 거래 · 저장 등, 접속은 `getConnectionCount` 가 센다). 종료 드레인은 이것이 0 이 될 때까지 기다린다.
         */
        virtual int32 getOpenJobCount() const = 0;
        /** @brief 종료 드레인이 시작될 때 한 번 불립니다. 새 일을 받지 않고 진행 중인 일을 마무리하며 클라이언트에 사유를 알립니다. */
        virtual void beginShutdown( const ServerShutdownNotice& notification ) = 0;
    };
} // namespace sw
