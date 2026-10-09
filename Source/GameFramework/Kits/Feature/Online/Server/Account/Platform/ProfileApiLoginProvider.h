/**
 * @file ProfileApiLoginProvider.h
 * @brief 액세스 토큰 조회형 제공자 — OIDC 가 없는 곳(네이버 등)을 "프로필 주소 + 응답 JSON 의 주체 id 경로" 설정만으로 확인합니다.
 * @details 표 글 = 액세스 토큰. `GET <프로필 주소>` 에 `Authorization: Bearer <토큰>` — 200 이면 경로의 값(글 · 숫자)이 주체, 401 · 403 은 거절, 그 밖(연결 · 시한 · 5xx)은 제공자 없음.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Account/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Server/Account/Platform/PlatformLoginProviderSettings.h"

namespace sw
{
    class HttpClient;
    class JsonValue;

    /**
     * @class ProfileApiLoginProvider
     * @brief 프로필 API 제공자입니다.
     */
    class SW_GF_API ProfileApiLoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief @p pHttpClient 는 빌려 쓰고 그 응답은 이 객체 혼자 거둔다. */
        ProfileApiLoginProvider( const PlatformLoginProviderSettings& settings, HttpClient* pHttpClient );

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        tick( int64 nowMs ) override;

        /** @brief JSON 에서 점 경로("response.id")의 값을 글로 찾습니다(숫자는 십진 글). */
        [[nodiscard]] static bool findPathText( const JsonValue& root, string_view path, string& outText );

    private:
        PlatformLoginProviderSettings     _settings;
        unordered_map<uint64, uint64>     _mapRequestToVerification;
        vector<PlatformLoginVerification> _listDone;
        HttpClient*                       _pHttpClient;
        uint64                            _nextVerificationId;
    };
} // namespace sw
