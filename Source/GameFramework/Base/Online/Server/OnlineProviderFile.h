/**
 * @file OnlineProviderFile.h
 * @brief 온라인 서버 키트의 외부 제공자 설정 파일(`{ "providers": [ { … }, … ] }`)을 읽는 한 곳입니다.
 * @details 계정(외부 로그인) · 경제(영수증) · 라이브 운영(푸시) 키트가 같은 모양의 파일을 저마다 읽었다(파일 읽기 · JSON · `providers` 배열 · 항목이 객체인지 · 신뢰 PEM 짝 검사가 세 벌).
 *          이제 그 틀은 여기 하나이고 키트는 항목 하나를 읽는 함수만 준다. 경로는 키트 옵션의 파일 경로 칸(`OnlineServerKitOptionField` 의 `std::optional<AbsolutePath>`)이 이미 풀었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct OnlineProviderFile
     * @brief 제공자 설정 파일 한 쌍(제공자 JSON · 신뢰 PEM)을 읽습니다.
     */
    struct SW_GF_API OnlineProviderFile
    {
        /** @brief 항목 하나(객체임은 확인했다)를 읽습니다. 틀리면 false 와 @p outError 입니다. */
        template <typename Config>
        using EntryReader = bool ( * )( const JSONValue& entry, Config& outConfig, string& outError );

        /**
         * @brief 제공자 파일 @p providersFile 의 항목을 모두 @p readEntry 로 읽어 @p outListConfig 에 담습니다.
         * @param pProvidersOption · pTrustOption 오류 글에 쓸 옵션 이름(`pushProvidersFile` · `pushTrustFile`)
         * @return 파일을 정하지 않았으면 빈 목록으로 true 다(제공자 없음). 신뢰 PEM 만 정했거나, 파일을 못 읽거나, JSON · 항목이 틀리면 false 와 @p outError 입니다.
         */
        template <typename Config>
        [[nodiscard]] static bool read( const std::optional<AbsolutePath>& providersFile, const std::optional<AbsolutePath>& trustFile, const utf8* pProvidersOption,
                                        const utf8* pTrustOption, EntryReader<Config> readEntry, vector<Config>& outListConfig, string& outError )
        {
            outListConfig.clear();
            JSONDocument      document;
            vector<JSONValue> listEntry;
            if ( readEntries( providersFile, trustFile, pProvidersOption, pTrustOption, document, listEntry, outError ) == false )
                return false;
            for ( const JSONValue& entry : listEntry )
            {
                if ( readEntry( entry, outListConfig.emplace_back(), outError ) == false )
                    return false;
            }
            return true;
        }

        /** @brief 신뢰 PEM 옵션을 제공자의 TLS 등록이 받는 글로 바꿉니다. 정하지 않았으면 빈 글(그때 `https://` 제공자는 등록이 거절한다)입니다. */
        [[nodiscard]] static string_view getTrustText( const std::optional<AbsolutePath>& trustFile ) { return trustFile.has_value() ? string_view( trustFile->str() ) : string_view{}; }

    private:
        /** @brief 파일을 읽고 `providers` 배열의 항목(모두 객체)을 @p outListEntry 에 담습니다. 핸들은 @p outDocument 를 가리킨다(읽기만 한다). */
        [[nodiscard]] static bool readEntries( const std::optional<AbsolutePath>& providersFile, const std::optional<AbsolutePath>& trustFile, const utf8* pProvidersOption,
                                               const utf8* pTrustOption, JSONDocument& outDocument, vector<JSONValue>& outListEntry, string& outError );
    };
} // namespace sw
