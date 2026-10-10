/**
 * @file LoadBotRunner.h
 * @brief 봇 실행기 — 전송 하나 · 끝점 하나 · 요청 클라이언트 하나에 봇(연결) 여럿. 늘리는 시간 동안 봇을 고르게 띄우고, 틱마다 봇의 다음 단계를 낸다.
 * @details - 전송은 빌려 쓴다(시험은 루프백 망의 전송, 실제는 `StreamTransportFactory::createPlatformTransport()` — Main 이 만든다). I/O 스레드 없이
 *            `tick` 이 `pollIO` 를 돈다 — 실행기 스레드 하나.
 *          - 시계: 기본은 `MonotonicClock`(지연은 마이크로초). 시험은 `setManualClock` 으로 바깥이 밀리초를 정한다(결정적 — 지연도 그 밀리초).
 *          - 받은 Message 프레임(알림)은 종류(첫 2 바이트)별로 센 뒤 그 봇에 넘긴다.
 *          - 시나리오를 다 돌았거나(모든 봇이 끝남) 늘린 뒤 `_durationSeconds` 가 지나면 `tick` 이 false 다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetTypes.h"

#include "OnlineLoadBot/LoadBotMetrics.h"
#include "OnlineLoadBot/LoadBotScenario.h"

namespace sw
{
    class IStreamTransport;
    class LoadBot;

    /** @brief 실행기 설정입니다. */
    struct LoadBotRunnerSettings
    {
        NetAddress _serverAddress{};
        int32      _botCountOverride{ 0 }; ///< 0 = 시나리오 값
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

        /** @brief @p pTransport 를 빌려 끝점 · 요청 클라이언트를 띄웁니다. 실패하면 false 와 @p outError. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const LoadBotScenario& scenario, const LoadBotRunnerSettings& settings, string& outError );
        void               shutdown();

        /** @brief 시험 — 시계를 바깥이 정한다(밀리초, 처음 값이 시작). 한 번이라도 부르면 그 뒤로 이 값만 쓴다. */
        void setManualClock( int64 nowMs );

        /** @brief 한 틱 — 전송 · 끝점 · 요청 시한을 돌고, 늘리는 중이면 봇을 더 띄우고, 봇마다 다음 단계를 본다. 시나리오가 끝났으면 false. */
        bool tick();

        const LoadBotMetrics&  getMetrics() const { return _metrics; }
        const LoadBotScenario& getScenario() const { return _scenario; }
        int32                  getBotCount() const { return static_cast<int32>( _listBot.size() ); }
        int32                  getRunningBotCount() const;
        /** @brief 시작(첫 틱)부터 지난 밀리초입니다. */
        int64 getElapsedMs() const { return getNowMs(); }

        // 봇이 부른다
        LoadBotMetrics&        getMutableMetrics() { return _metrics; }
        int64                  getNowMs() const;
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
        int64                        _manualNowMs;
        int64                        _manualStartMs;
        int32                        _launchedCount;
        uint8                        _bManualClock;
        uint8                        _bInitialized;
    };
} // namespace sw
