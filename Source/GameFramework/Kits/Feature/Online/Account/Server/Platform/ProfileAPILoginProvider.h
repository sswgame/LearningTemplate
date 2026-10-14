/**
 * @file ProfileAPILoginProvider.h
 * @brief 액세스 토큰 조회형 제공자입니다. OIDC 가 없는 곳(네이버 등)을 "프로필 주소와 응답 JSON 의 주체 id 경로" 설정만으로 확인합니다.
 * @details 티켓 문자열은 액세스 토큰이다. `GET <프로필 주소>` 에 `Authorization: Bearer <토큰>` 을 실어 보낸다.
 *          200 이면 경로의 값(문자열이나 숫자)이 주체이고, 401 과 403 은 거절이며, 그 밖(연결 실패, 시한, 5xx)은 제공자를 쓸 수 없는 것으로 본다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSettings.h"

namespace sw
{
    class HTTPClient;
    class JSONValue;

    /**
     * @class ProfileAPILoginProvider
     * @brief 프로필 API 제공자입니다.
     */
    class SW_KIT_API ProfileAPILoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief @p pHTTPClient 는 빌려 쓰며 그 응답은 이 객체만 회수합니다. */
        ProfileAPILoginProvider( const PlatformLoginProviderSettings& settings, HTTPClient* pHTTPClient );

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        tick( int64 nowMs ) override;

        /** @brief JSON 에서 점 경로("response.id")의 값을 문자열로 찾습니다. 숫자는 십진 문자열로 바꿉니다. */
        [[nodiscard]] static bool findPathText( const JSONValue& root, string_view path, string& outText );

    private:
        PlatformLoginProviderSettings     _settings;
        unordered_map<uint64, uint64>     _mapRequestToVerification;
        vector<PlatformLoginVerification> _listDone;
        HTTPClient*                       _pHTTPClient;
        uint64                            _nextVerificationID;
    };
} // namespace sw
