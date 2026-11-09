/**
 * @file GameFramework/Runtime/Online/Service/IOnlineLocalServer.h
 * @brief 개발 구성의 프로세스 안 온라인 서버를 세션에 붙이는 자리입니다 — 세션(`OnlineSession::startLocalServer`)은 서버 조립(`Runtime/Online/Server/`)의 형식을 모릅니다.
 * @details 서버 코드(`Server/OnlineLocalServer.cpp`)가 공장(`IOnlineLocalServerFactory`)을 모듈 부분으로 공개하고(`SW_MODULE_PART( Engine, … )`), 세션은 그 창구로만
 *          서버를 만들어 듭니다(세션과 수명이 같다). 클라이언트 구성은 이름이 `Server` 인 폴더를 짓지 않아(`SW_SERVER_ONLY_SOURCE_PATTERNS`) 공장이 없고,
 *          세션은 "이 구성에 서버 코드가 없다" 오류를 돌려줍니다 — 다른 것으로 대신하지 않습니다. `IGameClientPart` 의 반대쪽이고,
 *          언리얼 PIE 가 서버 모듈이 실린 에디터에서만 전용 서버를 띄우는 것과 같은 갈림이다(`WITH_SERVER_CODE`, 기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    class IClock;
    class IServiceStore;

    /** @class IOnlineLocalServer @brief 세션이 드는 프로세스 안 서버 하나입니다. 지우면 서버가 내려갑니다. 게임 스레드에서 부릅니다. */
    class IOnlineLocalServer
    {
    public:
        virtual ~IOnlineLocalServer() = default;

        /** @brief 받는 루프백 포트입니다(세션이 평문으로 붙는다). */
        [[nodiscard]] virtual uint16 getListenPort() const = 0;
        /** @brief 호스트 · 예약 작업 · 키트를 돌립니다. @p nowMs 는 서비스 시각입니다. */
        virtual void update( UnixMilliseconds nowMs ) = 0;
        /** @brief 서버의 저장소입니다. 개발 명령 · 시나리오의 준비 단계가 게임 스레드에서 작업을 맡깁니다. */
        [[nodiscard]] virtual IServiceStore* findStore() const = 0;
    };

    /** @class IOnlineLocalServerFactory @brief 프로세스 안 서버를 만드는 쪽입니다(서버 코드의 모듈 부분 — 클라이언트 구성에는 없다). */
    class IOnlineLocalServerFactory
    {
    public:
        virtual ~IOnlineLocalServerFactory() = default;

        /**
         * @brief @p serverConfigPath(전용 서버의 `-server-config` 와 같은 파일)와 옆의 온라인 설정으로 서버를 세우고 루프백의 아무 포트에서 평문으로 받기 시작합니다.
         * @param clock 메모리 캐시 만료를 재는 단조 시계입니다. 빌려 쓰며 서버보다 오래 살아야 합니다.
         * @return 설정을 읽지 못하거나 키트가 서지 못하면 오류입니다.
         */
        [[nodiscard]] virtual expected<unique_ptr<IOnlineLocalServer>> createLocalServer( string_view serverConfigPath, const IClock& clock, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw
