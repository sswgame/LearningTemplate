/**
 * @file AccountDevOnline.cpp
 * @brief DevOnline 단위 — 계정 키트(서버)의 개발 대체물(개발 티켓 키 · 가벼운 비밀번호 해시 · 테스트 계정 시드 읽기)을 확장점 `AccountDevOptions` 에 올립니다.
 * @details 배포 구성은 이 파일을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`) — 그 구성에서 개발 옵션을 쓰면 키트 기동이 실패한다.
 */
#include "pch.h"

#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/AccountDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"

namespace sw
{
    namespace
    {
        struct AccountDevOnlineInternal
        {
            static constexpr uint32 kLightHashMemoryKiB  = 256; ///< 테스트용 가벼운 Argon2id
            static constexpr uint32 kLightHashIterations = 1;
            static constexpr uint8  kDevTicketKeyByte    = 0x5A; ///< 개발 티켓 주 키의 바이트(모든 바이트 같은 값) — 로그인 서버와 게임 서버가 같은 값을 본다

            /** @brief `seedTestAccounts` 옵션(`이름:비밀번호,…`)을 읽어 @p outListCredential 에 더합니다. 형식이 틀리면 false 와 까닭입니다. */
            [[nodiscard]] static bool parseSeedTestAccounts( const string& text, vector<LoginCredential>& outListCredential, string& outError )
            {
                size_t begin = 0;
                while ( begin <= text.size() )
                {
                    size_t end = text.find( ',', begin );
                    if ( end == string::npos )
                        end = text.size();
                    const string item  = text.substr( begin, end - begin );
                    const size_t colon = item.find( ':' );
                    if ( colon == string::npos || colon == 0 || colon + 1 >= item.size() )
                    {
                        outError = "seedTestAccounts entries must look like name:password, got '" + item + "'";
                        return false;
                    }
                    LoginCredential credential;
                    credential._loginName = item.substr( 0, colon );
                    credential._password  = item.substr( colon + 1 );
                    outListCredential.push_back( std::move( credential ) );
                    begin = end + 1;
                }
                return true;
            }

            static constexpr AccountDevOptions kDevOptions{
                NetPasswordHashParams{ kLightHashMemoryKiB, kLightHashIterations },
                &parseSeedTestAccounts, kDevTicketKeyByte
            };
        };
    } // namespace

    SW_REGISTER_DEV_ONLINE_EXTENSION( AccountDevOptions, AccountDevOnlineInternal::kDevOptions );
} // namespace sw
