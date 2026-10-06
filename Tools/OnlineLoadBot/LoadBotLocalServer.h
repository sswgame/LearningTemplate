/**
 * @file LoadBotLocalServer.h
 * @brief 한 프로세스 온라인 서버 — 부하 봇이 겨눌 서비스 호스트 하나에 키트 일곱(계정 · 서버 디렉터리 · 채팅 · 친구 · 순위 · 매칭 · 라이브 운영)을 올린 조립입니다.
 * @details - 저장소 · 캐시 · 버스는 메모리 구현(같은 프로세스). 게임 서버 하나를 디렉터리에 등록해 매칭 배정이 그리로 간다.
 *          - 전용 서버 실행 파일(`Server`)이 온라인 서비스를 조립하기 전까지 봇의 `--local-server` 와 끝단 시험(`OnlineLoadBotTest`)이 같은 조립을 쓴다.
 *          - 내리는 순서는 온라인 계약대로 키트 → 호스트 → 저장소 · 캐시(계정은 로그인 서비스 계약대로 저장소 완료를 거둔 뒤).
 *          - 개발 도구용 조립이다: 한 주소에서 오는 봇 수천을 받도록 주소마다 요청 · 로그인 시도 제한을 크게 둔다(실제 서버 설정은 서버 조립의 몫).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IStreamTransport;
    class OnlineServiceHost;

    /** @brief 조립 설정입니다. */
    struct LoadBotLocalServerSettings
    {
        vector<string> _listBoardId{};          ///< 서버만 점수를 내는 순위표 — 비면 "kills"
        string         _region{ "kr" };         ///< 게임 서버 지역
        uint64         _serverId{ 1 };          ///< 버스 · 접속 상태의 서버 id
        int32          _maxConnections{ 1024 }; ///< 받을 연결 상한(봇 수보다 크게)
        int32          _gameServerCapacity{ 100000 };
        uint16         _port{ 7100 };
        uint8          _bLightPasswordHash{ SW_FALSE }; ///< 시험 — 가벼운 Argon2id(실제 비용을 재려면 끈다)
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

        /** @brief 호스트 틱 · 게임 서버 하트비트입니다. @p nowMs 는 서비스 시각(실제 실행은 UTC 밀리초, 시험은 가짜 시각). */
        void tick( int64 nowMs );

        OnlineServiceHost& getHost();
        uint16             getListenPort() const;

    private:
        struct Parts;

        unique_ptr<Parts> _parts;
        uint8             _bInitialized;
    };
} // namespace sw
