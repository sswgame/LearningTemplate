/**
 * @file ServerServiceRegistry.h
 * @brief 전용 서버가 띄우는 서버 서비스의 레지스트리입니다. 모듈(GameFramework, 키트, 게임)이 정적 등록기로 팩토리를 올리면 서버 실행 파일이 기동 때 만들어 돌립니다.
 * @details - 서버 서비스는 게임 틱과 따로 서버 프로세스와 함께 사는 일입니다(온라인 서비스 호스트, 서버 등록). 언리얼의 `UEngineSubsystem`(모듈이 로드되면 엔진이 만든다)과
 *            Nakama 런타임 모듈의 `InitModule` 에 해당합니다.
 *          - 서비스는 `ServerServiceContext` 로 서버의 설정, 지표 레지스트리, 상태 레지스트리를 받습니다. 게임과 키트가 서버 지표와 상태를 받는 길은 이것 하나입니다.
 *          - 팩토리는 등록기(`SW_REGISTER_SERVER_SERVICE`)가 가진 정적 객체이고, 그 모듈이 언로드되면 스스로 빠집니다. 레지스트리는 목록만 가지고 인스턴스를 만들지 않습니다.
 *            인스턴스의 수명은 `ServerServiceManager` 가 정합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ShutdownSignal.h"
#include "Core/Time/TimePoint.h"

#include "Engine/DedicatedServer/ServiceFactoryList.h"

namespace sw
{
    struct ServerConfig;

    class ConfigManager;
    class MetricRegistry;
    class ServiceHealthRegistry;

    /**
     * @brief 서버 서비스가 기동 때 받는 서버 프로세스의 문맥입니다. 포인터는 모두 빌려 쓰며 서비스보다 오래 삽니다.
     * @details 서버 서비스가 아닌 게임과 키트 코드(네트워크 세션)는 이것을 `engine::findSubsystem<ServerServiceContext>()` 로 찾는다 — 전용 서버가 게임을 세우기 전에
     *          호스트 범위에 빌려 걸고(`SubsystemScope::bind`) 게임이 내려간 뒤 푼다. App · 테스트에는 없어 nullptr 이다(정적 칸을 두지 않는다).
     */
    struct ServerServiceContext
    {
        string                 _serverConfigPath{};         ///< 서버 설정 파일 경로(서비스가 옆 파일을 찾을 때 쓴다)
        const ServerConfig*    _pServerConfig{ nullptr };   ///< 읽은 서버 설정(`Config/Server/<게임>.json`)
        ConfigManager*         _pConfigManager{ nullptr };  ///< 서비스가 자기 설정 파일을 읽을 때 쓴다
        MetricRegistry*        _pMetricRegistry{ nullptr }; ///< `/metrics` 로 나가는 지표 레지스트리
        ServiceHealthRegistry* _pHealthRegistry{ nullptr }; ///< `/healthz`, `/readyz` 가 읽는 상태 레지스트리
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerShutdownNotice
     * @brief 서버가 종료 드레인에 들어가며 서비스 · 키트에 알리는 것입니다(`IServerService::beginShutdown`, 온라인 키트의 `IOnlineServerKit::beginShutdown`).
     * @details 언리얼 `AGameModeBase::HandleDisconnect` · 유니티 Multiplay 의 "서버가 곧 내려간다" 통지처럼, 키트가 진행 중인 매치 · 거래를 마무리하고 클라이언트에 끊김 사유를
     *          알릴 기회다. 기다림은 `_graceSeconds`(0 이면 바로 내려간다 — 알리기만 하고 마무리를 기다리지 않는다) 안에서 `getOpenWorkCount` 가 0 이 될 때까지다.
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
     * @brief 서버 프로세스와 함께 사는 서비스 하나입니다. 모든 함수는 서버의 게임 스레드에서 불립니다.
     */
    class SW_API IServerService
    {
    public:
        IServerService()          = default;
        virtual ~IServerService() = default;

        IServerService( const IServerService& )            = delete;
        IServerService& operator=( const IServerService& ) = delete;

        /**
         * @brief 서비스를 시작합니다. 설정상 할 일이 없으면 아무것도 하지 않고 true 를 반환합니다.
         * @return 시작해야 하는데 실패하면 false 와 @p outError 입니다. 서버는 기동을 멈춥니다.
         */
        [[nodiscard]] virtual bool initialize( const ServerServiceContext& context, string& outError ) = 0;
        /** @brief 빌린 것을 모두 돌려주고 멈춥니다. `initialize` 가 실패했어도 불립니다. */
        virtual void shutdown() = 0;
        /** @brief 서버 루프가 돌 때마다 불립니다. 게임 틱이 멈춰 있어도(Dev `pause`) 불립니다. @p nowMs 는 UTC 밀리초입니다. */
        virtual void update( UnixMilliseconds nowMs ) = 0;
        /** @brief 이 서비스가 받고 있는 연결 수입니다(지표 `server_service_connections`). */
        virtual int32 getConnectionCount() const { return 0; }
        /**
         * @brief 이 서비스가 끝내기를 기다려야 하는 진행 중인 일의 수입니다(매치 · 거래 · 저장 등 — 접속은 `getConnectionCount` 가 센다). 종료 드레인은 이것이 0 이 될 때까지 기다린다.
         * @details 기본 0 은 "기다릴 일이 없다" 이다. 게임 스레드에서 불린다.
         */
        virtual int32 getOpenWorkCount() const { return 0; }
        /** @brief 종료 드레인이 시작될 때 한 번 불립니다. 새 일을 받지 않고 진행 중인 일을 마무리하며 클라이언트에 사유를 알립니다. 기본은 아무 일도 하지 않습니다. */
        virtual void beginShutdown( const ServerShutdownNotice& notice ) { (void)notice; }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 서비스 팩토리 하나입니다. 등록기가 가진 정적 객체입니다. 이름과 시작 순서는 노드에 있습니다. */
    struct ServerServiceFactory : ServiceFactoryNode
    {
        using CreateFunction = unique_ptr<IServerService> ( * )();

        CreateFunction _pCreate{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ServerServiceRegistry
     * @brief 서버 서비스 팩토리의 레지스트리입니다. 프로세스에 하나이며 Engine 에 있습니다(모듈은 모두 Engine 을 링크합니다).
     */
    struct SW_API ServerServiceRegistry
    {
        /** @brief 팩토리를 올립니다. 같은 이름이 있으면 오류 로그를 남기고 올리지 않습니다. */
        static void registerFactory( ServerServiceFactory* pFactory );
        /** @brief 팩토리를 내립니다. 없으면 아무것도 하지 않습니다. */
        static void unregisterFactory( ServerServiceFactory* pFactory );
        /** @brief 올라온 팩토리를 시작 순서(같으면 이름 순)로 @p outListFactory 에 담습니다. 비운 뒤 채웁니다. */
        static void collectFactories( vector<ServerServiceFactory*>& outListFactory );
        /** @brief 올라 있는 팩토리 수입니다. */
        static int32 getFactoryCount();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServerServiceRegistrar
     * @brief 서버 서비스 타입 하나를 정적 초기화 때 올리고, 모듈이 언로드될 때 내립니다. 직접 쓰지 않고 `SW_REGISTER_SERVER_SERVICE` 를 씁니다.
     */
    template <typename TService>
    class ServerServiceRegistrar
    {
    public:
        ServerServiceRegistrar( const utf8* pName, int32 startOrder )
            : _factory{}
        {
            _factory._pName   = pName;
            _factory._order   = startOrder;
            _factory._pCreate = &ServerServiceRegistrar::create;
            ServerServiceRegistry::registerFactory( &_factory );
        }
        ~ServerServiceRegistrar() { ServerServiceRegistry::unregisterFactory( &_factory ); }

        ServerServiceRegistrar( const ServerServiceRegistrar& )            = delete;
        ServerServiceRegistrar& operator=( const ServerServiceRegistrar& ) = delete;

    private:
        static unique_ptr<IServerService> create() { return make_unique<TService>(); }

        ServerServiceFactory _factory;
    };
} // namespace sw

/**
 * @brief 서버 서비스 타입을 레지스트리에 올립니다. 그 타입을 정의하는 `.cpp` 의 `namespace sw` 안, 익명 네임스페이스 밖에서 한 번 씁니다.
 * @param Type `IServerService` 파생 타입
 * @param Name 로그와 오류에 쓰는 이름(문자열 리터럴)
 * @param StartOrder 시작 순서(작은 것부터)
 */
#define SW_REGISTER_SERVER_SERVICE( Type, Name, StartOrder )                \
    const ::sw::ServerServiceRegistrar<Type> s_serverServiceRegistrar##Type \
    {                                                                       \
        Name, StartOrder                                                    \
    }
