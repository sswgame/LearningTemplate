/**
 * @file OnlineSession.h
 * @brief 게임의 온라인 세션입니다. 서비스 연결 하나와 올라온 키트 클라이언트를 묶어, 게임은 `game::getSubsystem<OnlineSession>().get<MailboxClient>()` 만 부릅니다.
 * @details - 게임 인스턴스가 소유합니다(`GameInstanceSubsystem`). 서버 주소 · 믿을 인증서 · 빌드 글은 팩의 gamesettings 입니다(`GameSettings::_online*`). 온라인을 쓰지 않는 게임은 적지 않고, 그때 세션은 아무 일도 하지 않습니다.
 *          - 키트 클라이언트는 정적 등록(`SW_REGISTER_ONLINE_CLIENT`)에서 초기화 때 만들어 연결에 묶습니다. 등록을 하나 빠뜨려 응답이 오지 않는 조립 실수가 없습니다.
 *          - 연결은 게임이 `connect` 로 엽니다(타이틀 · 로그인 화면). 기동에서 자동으로 열지 않습니다 - 언리얼 CommonUser 의 로그인 호출처럼 게임이 시점을 정합니다(기억에 의존).
 *            끊김과 물러남은 `OnlineServiceClient` 가 맡고, 프레임마다 `GameInstance::update` 가 `update` 를 부릅니다. 전송 스레드 없이 게임 스레드에서 돕니다.
 *          - 개발 구성은 `startLocalServer` 로 이 프로세스 안에 서버 조립을 세워 붙을 수 있습니다(평문 루프백). 언리얼 PIE 의 전용 서버, UGS 의 에디터 안 서비스 자리이고,
 *            저장소 · 키트 · 제공자는 전용 서버와 같은 설정 파일이 고릅니다. 시나리오가 끝단을 값으로 판정하는 길입니다. 세션은 서버 코드를 모르고 창구
 *            (`IOnlineLocalServerFactory`)로만 만듭니다 — 클라이언트 구성에는 창구가 없어 오류입니다.
 *          UE5 Online Services 의 `GetServices()->GetXInterface()`, Nakama 의 `client` + `socket` 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/Clock.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"

namespace sw
{
    struct GameSettings;

    class IOnlineLocalServer;
    class IServiceStore;
    class IStreamTransport;
    class ITLSContext;

    /**
     * @class OnlineSession
     * @brief 게임의 온라인 세션입니다. 게임 스레드에서 씁니다.
     */
    class SW_GF_API OnlineSession final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "OnlineSession";

        /** @param settings 팩 설정입니다. 게임 인스턴스의 칸이라 세션보다 오래 삽니다. */
        explicit OnlineSession( const GameSettings& settings );
        ~OnlineSession() override;

        OnlineSession( const OnlineSession& )            = delete;
        OnlineSession& operator=( const OnlineSession& ) = delete;

        // ISubsystem - 키트 클라이언트를 만들어 묶는다. 연결은 `connect` 가 연다.
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief gamesettings 의 서버(개발 구성은 `-gv_onlineServerAddress` 가 덮어쓴 값 — `IGameSettingsOverride`)에 TLS 로 연결을 겁니다. 결과는 `getState` 입니다.
         * @return 주소 · 빌드 글이 없거나 TLS 문맥을 만들지 못하면 오류입니다. 평문이나 다른 서버로 대신하지 않습니다.
         */
        [[nodiscard]] expected<void> connect();
        /** @brief 전송과 보안을 직접 골라 연결합니다(테스트 · `startLocalServer`). @p pTransport 는 빌려 쓰고 세션보다 오래 삽니다. */
        [[nodiscard]] expected<void> connectTo( IStreamTransport* pTransport, const OnlineClientSecurity& security, const NetAddress& address );
        /**
         * @brief 이 프로세스 안에 서버 조립을 세우고 평문 루프백으로 붙습니다(개발 구성만). @p serverConfigPath 는 전용 서버의 `-server-config` 와 같은 파일이고 옆의 온라인 설정을 함께 읽습니다.
         * @return 배포 구성이거나, 서버 코드가 없는 구성(Client)이거나, 설정을 읽지 못하거나, 키트가 서지 못하면 오류입니다.
         */
        [[nodiscard]] expected<void> startLocalServer( string_view serverConfigPath );
        /** @brief 연결을 닫고 안에 세운 서버가 있으면 내립니다. 두 번 불러도 됩니다. */
        void disconnect();

        /** @brief 지금 시각으로 `updateAt` 을 부릅니다. 연결이 없으면 바로 돌아옵니다. `GameInstance::update` 가 프레임마다 부릅니다. */
        void update();
        /** @brief 서비스 연결(전송 · 요청 시한 · 다시 연결)과 안에 세운 서버를 돌립니다. */
        void updateAt( MonotonicMilliseconds nowMs );

        /** @brief 올라온 키트 클라이언트 @p T 입니다. 그 키트를 싣지 않았으면 nullptr 입니다. */
        template <class T>
        [[nodiscard]] T* find() const
        {
            return static_cast<T*>( findByType( getSubsystemTypeHash<T>() ) );
        }
        /** @brief 올라온 키트 클라이언트 @p T 입니다. 없으면 멈춥니다 - 게임 매니페스트에 키트 의존을 적지 않은 결함입니다. */
        template <class T>
        [[nodiscard]] T& get() const
        {
            T* const pClient = find<T>();
            if ( pClient == nullptr )
                SW_FATAL( "The kit of this online client is not in the game's module manifest" );
            return *pClient;
        }

        /** @brief 안에 세운 서버(`startLocalServer`)의 저장소입니다. 없으면 nullptr 입니다. 개발 명령 · 시나리오의 준비 단계가 게임 스레드에서 작업을 맡깁니다. */
        [[nodiscard]] IServiceStore* findLocalServerStore() const;

        OnlineServiceClient& getServiceClient() { return _client; }
        OnlineClientState    getState() const { return _client.getState(); }
        bool                 isReady() const { return _client.isReady(); }
        bool                 hasLocalServer() const { return _localServer != nullptr; }
        int32                getClientCount() const { return static_cast<int32>( _listClient.size() ); }

    private:
        struct ClientEntry
        {
            unique_ptr<IOnlineClientService> _client{};
            uint64                           _typeHash{ 0 };
        };

        [[nodiscard]] IOnlineClientService* findByType( uint64 typeHash ) const;

    private:
        const GameSettings&            _settings;
        SystemMonotonicClock           _clock; ///< 안에 세운 서버가 빌린다. 서버보다 먼저 선언한다
        unique_ptr<IOnlineLocalServer> _localServer;
        unique_ptr<ITLSContext>        _tlsContext; ///< 연결이 빌린다. `_client` 보다 먼저 선언해 나중에 지운다
        unique_ptr<IStreamTransport>   _transport;
        OnlineServiceClient            _client;
        vector<ClientEntry>            _listClient; ///< `_client` 보다 먼저 지워진다 - 소멸자가 스스로 등록을 푼다
        uint8                          _bConnected : 1;
    };
} // namespace sw
