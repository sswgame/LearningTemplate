/**
 * @file LoadBotRunner.h
 * @brief 봇 실행기 — 전송 하나 · 엔드포인트 하나 · 요청 클라이언트 하나에 봇(연결) 여럿. 늘리는 시간 동안 봇을 고르게 띄우고, 틱마다 봇의 다음 단계를 낸다.
 * @details - 전송은 빌려 쓴다(테스트는 루프백 망의 전송, 실제는 `StreamTransportFactory::createPlatformTransport()` — Main 이 만든다). I/O 스레드 없이
 *            `tick` 이 `pollIO` 를 돈다 — 실행기 스레드 하나.
 *          - 시계: 기본은 `MonotonicClock`(지연은 마이크로초). 테스트는 `setManualClock` 으로 바깥이 밀리초를 정한다(결정적 — 지연도 그 밀리초).
 *          - 받은 Message 프레임(알림)은 종류(첫 2 바이트)별로 센 뒤 그 봇에 넘긴다.
 *          - 시나리오를 다 돌았거나(모든 봇이 끝남) 늘린 뒤 `_durationMs` 가 지나면 `tick` 이 false 다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Time/TimePoint.h"

#include "Programs/OnlineLoadBot/LoadBotMetrics.h"
#include "Programs/OnlineLoadBot/LoadBotScenario.h"

namespace sw
{
    class IStreamTransport;
    class ITLSContext;
    class LoadBot;

    /** @brief 실행기 설정입니다. */
    struct LoadBotRunnerSettings
    {
        NetAddress   _serverAddress{};
        NetAddress   _gameAddress{};          ///< 게임 UDP 포트 — 시나리오의 `game_connect` 가 붙는다. 비어 있으면(`isValid` 거짓) 그 동작은 `game.noaddress` 오류다
        ITLSContext* _pTLSContext{ nullptr }; ///< 있으면 모든 연결이 TLS 1.3(전용 서버의 서비스 포트). 빌려 쓰며 실행기보다 오래 산다
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoadBotRunner
     * @brief 봇 실행기입니다.
     */
    class LoadBotRunner final : public IStreamEndpointListener
    {
    public:
        LoadBotRunner();
        ~LoadBotRunner() override;

        LoadBotRunner( const LoadBotRunner& )            = delete;
        LoadBotRunner& operator=( const LoadBotRunner& ) = delete;

        /** @brief @p pTransport 를 빌려 엔드포인트 · 요청 클라이언트를 띄웁니다. 실패하면 false 와 @p outError. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const LoadBotScenario& scenario, const LoadBotRunnerSettings& settings, string& outError );
        void               shutdown();

        /** @brief 테스트 — 시계를 바깥이 정한다(밀리초, 처음 값이 시작). 한 번이라도 부르면 그 뒤로 이 값만 쓴다. */
        void setManualClock( MonotonicMilliseconds nowMs );

        /** @brief 한 틱 — 전송 · 엔드포인트 · 요청 시한을 돌고, 늘리는 중이면 봇을 더 띄우고, 봇마다 다음 단계를 본다. 시나리오가 끝났으면 false. */
        bool update();

        const LoadBotMetrics&        getMetrics() const { return _metrics; }
        const LoadBotScenario&       getScenario() const { return _scenario; }
        const LoadBotRunnerSettings& getSettings() const { return _settings; }
        int32                        getBotCount() const { return static_cast<int32>( _listBot.size() ); }
        int32                        getRunningBotCount() const;
        /** @brief 시작(첫 틱)부터 지난 밀리초입니다. */
        int64 getElapsedMs() const { return getNowMs() - kRunStart; }

        // 봇이 부른다
        LoadBotMetrics& getMutableMetrics() { return _metrics; }
        /** @brief 실행기 시각의 기원입니다. 봇의 시한과 시작 순서가 같은 단조 시각에서 잰다. */
        static constexpr MonotonicMilliseconds kRunStart{ 0 };

        MonotonicMilliseconds  getNowMs() const;
        int64                  getNowUs() const;
        LoadBot*               findBot( int32 botIndex ) const;
        StreamConnectionHandle connectBot( int32 botIndex );
        void                   closeBot( StreamConnectionHandle handle );

        // IStreamEndpointListener — `tick` 안
        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override;
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        LoadBot* findBotByConnection( StreamConnectionHandle handle ) const;
        int64    computeLaunchMs( int32 botIndex ) const;

        vector<unique_ptr<LoadBot>>  _listBot;
        unordered_map<uint64, int32> _mapConnectionToBot; ///< 연결(packed) → 봇 번호
        LoadBotScenario              _scenario;
        LoadBotRunnerSettings        _settings;
        LoadBotMetrics               _metrics;
        StreamMessageEndpoint        _endpoint;
        NetRequestClient             _requestClient;
        IStreamTransport*            _pTransport;
        int64                        _startUs;
        MonotonicMilliseconds        _manualNowMs;
        MonotonicMilliseconds        _manualStartMs;
        int32                        _launchedCount;
        uint8                        _bManualClock;
        uint8                        _bInitialized;
    };
} // namespace sw
