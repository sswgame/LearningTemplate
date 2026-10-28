/**
 * @file LoadBotLocalServer.h
 * @brief 한 프로세스 온라인 서버입니다. 부하 봇이 겨눌 서비스 호스트 하나에 키트 일곱(계정 · 서버 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브 운영)을 올립니다.
 * @details - 조립은 전용 서버와 같은 `OnlineServerRuntime` 이다. 이 클래스는 봇에 맞는 온라인 설정(키트 목록과 옵션)을 만들어 넘길 뿐이다.
 *          - 저장소 · 캐시 · 버스는 메모리 구현(같은 프로세스)이다. 이 서버를 게임 서버로 디렉터리에 등록해 매칭 배정이 그리로 간다.
 *          - 봇의 `--local-server` 와 끝단 테스트(`OnlineLoadBotTest`)가 쓴다. 실제 서버(`Server`)에 붙이려면 봇의 `--server` 를 쓴다.
 *          - 개발 도구용 설정이다: 한 주소에서 오는 봇 수천을 받도록 주소마다 요청 · 로그인 시도 제한을 크게 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Clock.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    class IStreamTransport;
    class OnlineServerRuntime;
    class OnlineServiceHost;

    /** @brief 조립 설정입니다. */
    struct LoadBotLocalServerSettings
    {
        vector<string>   _listBoardID{};          ///< 서버만 점수를 내는 순위표 — 비면 "kills"
        string           _region{ "kr" };         ///< 게임 서버 지역
        UnixMilliseconds _startMs{ 0 };           ///< 서비스 시각의 시작. 0 이면 지금 — 가짜 시계 테스트는 첫 `tick` 과 같은 값을 준다
        const IClock*    _pClock{ nullptr };      ///< 필수. 메모리 캐시 만료를 재는 단조 시계(실행은 `SystemMonotonicClock`, 결정적 테스트는 `ManualClock` 을 틱마다 민다). 빌려 쓴다
        uint64           _serverID{ 1 };          ///< 버스 · 접속 상태 · 디렉터리의 서버 id
        int32            _maxConnections{ 1024 }; ///< 받을 연결 상한(봇 수보다 크게)
        int32            _gameServerCapacity{ 100000 };
        uint16           _port{ 7100 };
        uint8            _bLightPasswordHash{ SW_FALSE }; ///< 테스트 — 가벼운 Argon2id(실제 비용을 측정하려면 끈다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoadBotLocalServer
     * @brief 한 프로세스 온라인 서버입니다(실행기 스레드 하나에서 `tick`).
     */
    class LoadBotLocalServer
    {
    public:
        LoadBotLocalServer();
        ~LoadBotLocalServer();

        LoadBotLocalServer( const LoadBotLocalServer& )            = delete;
        LoadBotLocalServer& operator=( const LoadBotLocalServer& ) = delete;

        /** @brief @p pTransport 를 빌려(I/O 스레드 없이 — `tick` 이 돈다) 서비스를 올리고 받기 시작합니다. 실패하면 false 와 @p outError. */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const LoadBotLocalServerSettings& settings, string& outError );
        void               shutdown();

        /** @brief 호스트 틱 · 서버 등록 하트비트입니다. @p nowMs 는 서비스 시각(실제 실행은 UTC 밀리초, 테스트는 가짜 시각) — 캐시 만료도 이 시각으로 측정한다. */
        void update( UnixMilliseconds nowMs );

        OnlineServiceHost& getHost();
        uint16             getListenPort() const;

    private:
        unique_ptr<OnlineServerRuntime> _runtime;
    };
} // namespace sw
