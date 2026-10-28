/**
 * @file PlatformServices.h
 * @brief 스토어 플랫폼(Steam · Epic …)의 클라이언트 쪽 서비스 계약입니다 — 업적 · 통계 · 클라우드 파일 · 리치 프레전스 · 로그인 인증 티켓.
 * @details 게임 코드는 이 인터페이스만 부른다(언리얼 `IOnlineSubsystem` · 유니티 Game Services 와 같은 자리). 플랫폼 제품 이름은 구현 파일(`SteamPlatformServices`) 안과 설정에만 쓴다.
 *          - **스토어 이름**(`getStoreName`)이 서버 쪽 영수증 검증 제공자의 이름(`IReceiptValidator::getStoreName`, 경제 키트)과 **같은 글**이다 — 클라이언트가 결제 영수증을 서버로 보낼 때
 *            이 이름을 `storeName` 에 실으면 그 스토어의 검증기가 받는다(`PlatformStoreName`). 이름이 어긋나면 서버가 모르는 스토어로 거절한다.
 *          - **로그인 티켓**(`requestAuthTicket`)은 서버 로그인 키트의 플랫폼 로그인 제공자(`ticketQuery` 등)에 넘기는 스토어 인증 글이다.
 *          - 외부 SDK 는 이 파일도 구현도 링크하지 않는다. SDK 를 쥔 쪽(별도 플러그인 모듈)이 SDK 호출 묶음(`ISteamApiBridge`)을 구현해 넘긴다.
 *          - 동기 호출 모양이다(결과는 이번 프레임에 돌아오고 스토어에 쓰는 것은 SDK 가 비동기로 한다). `update` 를 프레임마다 부른다(SDK 콜백 펌프).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 스토어 이름 상수입니다. 서버의 영수증 검증 제공자(`IReceiptValidator::getStoreName`)와 같은 값이어야 합니다. */
    struct PlatformStoreName
    {
        static constexpr const utf8* kSteam = "steam";
    };

    /**
     * @class IPlatformServices
     * @brief 스토어 플랫폼 서비스 하나입니다. 게임 스레드에서만 부릅니다.
     */
    class SW_GF_API IPlatformServices
    {
    public:
        IPlatformServices()          = default;
        virtual ~IPlatformServices() = default;

        IPlatformServices( const IPlatformServices& )            = delete;
        IPlatformServices& operator=( const IPlatformServices& ) = delete;

        /** @brief 이 플랫폼의 스토어 이름입니다(`PlatformStoreName`). 영수증 검증 제공자의 이름과 같습니다. */
        virtual const utf8* getStoreName() const = 0;
        /** @brief 업적 하나를 풉니다. 이미 풀렸어도 true 입니다. 모르는 업적 · 플랫폼 거절이면 false 입니다. */
        [[nodiscard]] virtual bool unlockAchievement( string_view achievementID ) = 0;
        /** @brief 통계 값을 정합니다(플랫폼의 업적 진행이 통계에 묶인 경우). */
        [[nodiscard]] virtual bool setStat( string_view statID, int64 value ) = 0;
        /** @brief 클라우드 저장 파일을 씁니다(같은 이름은 덮어쓴다). */
        [[nodiscard]] virtual bool writeCloudFile( string_view fileName, const vector<uint8>& bytes ) = 0;
        /** @brief 클라우드 저장 파일을 읽습니다. 없으면 false 이고 @p outBytes 는 비어 있습니다. */
        [[nodiscard]] virtual bool readCloudFile( string_view fileName, vector<uint8>& outBytes ) = 0;
        /** @brief 친구 목록에 보일 리치 프레전스 한 칸을 정합니다. */
        [[nodiscard]] virtual bool setRichPresence( string_view key, string_view value ) = 0;
        /**
         * @brief 서버 로그인에 내밀 스토어 인증 티켓(바이트)을 받습니다. 서버가 스토어에 물어 이 계정이 맞는지 확인한다.
         * @return 플랫폼에 로그인돼 있지 않으면 false 입니다.
         */
        [[nodiscard]] virtual bool requestAuthTicket( vector<uint8>& outTicket ) = 0;
        /** @brief 프레임마다 부릅니다(SDK 콜백 펌프). */
        virtual void update() = 0;
    };
} // namespace sw
