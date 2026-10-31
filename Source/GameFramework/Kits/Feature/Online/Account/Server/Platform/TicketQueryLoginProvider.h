/**
 * @file TicketQueryLoginProvider.h
 * @brief 세션 티켓 확인 API 형 제공자입니다(OnlineKits A5). 티켓(16 진 글)과 서버 비밀 키를 쿼리 인자로 보내고, 결과 경로의 글이 성공 값이면 주체 경로의 값이 주체입니다.
 * @details 게임 플랫폼의 사용자 인증 티켓 확인(웹 API)을 제품 코드 없이 **데이터**로 정의한다(`PlatformLoginProviderKind::TicketQuery`). 예:
 *          `{"name":"steam","kind":"ticketQuery","profileUrl":"https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/?appid=<앱>",
 *            "ticketParam":"ticket","keyParam":"key","keySecretEnvironment":"<변수>","resultPath":"response.params.result","resultValue":"OK",
 *            "subjectPath":"response.params.steamid"}`
 *          - 키는 설정 파일에 쓰지 않고 환경 변수(서버 비밀)에서 읽는다. 읽지 못하면 제공자를 만들지 않는다(`create` 가 nullptr — 서버 기동 실패).
 *          - 티켓은 16 진 글이어야 한다(쿼리에 그대로 싣는다). 아니면 확인하지 않고 거절한다.
 *          - 200 이고 결과 글이 성공 값이면 성공, 200 인데 결과가 다르거나 주체가 없으면 거절, 401 · 403 은 거절, 그 밖(연결 실패 · 시한 · 5xx)은 쓸 수 없음이다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

namespace sw
{
    class HTTPClient;

    /**
     * @class TicketQueryLoginProvider
     * @brief 티켓 확인 API 제공자입니다.
     */
    class SW_KIT_SERVER_API TicketQueryLoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief 비밀 키를 환경 변수에서 읽어 만듭니다. 읽지 못하거나 키가 쿼리에 실을 수 없는 글이면 오류를 남기고 nullptr 입니다. */
        static unique_ptr<IPlatformLoginProvider> create( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient );
        /** @brief 키를 이미 가진 쪽(테스트)이 만듭니다. @p pHTTPClient 는 빌려 쓰며 그 응답은 이 객체만 회수합니다. */
        TicketQueryLoginProvider( const PlatformLoginProviderConfig& settings, string key, HTTPClient* pHTTPClient );
        ~TicketQueryLoginProvider() override;

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        update( UnixMilliseconds nowMs ) override;

        /** @brief 쿼리에 그대로 실을 수 있는 글(영숫자 · `-` · `_` · `.`)이면 true 입니다. 키와 티켓을 검사합니다. */
        static bool isQuerySafe( string_view text );
        /** @brief 확인 요청 주소입니다(설정 주소에 키 · 티켓 인자를 덧붙인다). 테스트가 봅니다. */
        string makeRequestURL( string_view ticket ) const;

    private:
        PlatformLoginProviderConfig       _settings;
        string                            _key;
        unordered_map<uint64, uint64>     _mapRequestToVerification;
        vector<PlatformLoginVerification> _listDone;
        HTTPClient*                       _pHTTPClient;
        IDAllocator<uint64>               _verificationIDAllocator;
    };
} // namespace sw
