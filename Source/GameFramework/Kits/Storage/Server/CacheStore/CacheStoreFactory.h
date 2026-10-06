/**
 * @file CacheStoreFactory.h
 * @brief 서버 설정의 캐시 항목(`ServerCacheEntry` — 드라이버 이름 · 끝점 · 비밀) 하나 → 휘발성 저장 앞입니다.
 * @details - "memory": 기반의 메모리 구현(서버 한 대 · 개발 — 같은 프로세스의 앞들이 데이터 하나를 나눠 쓴다).
 *          - "resp": `RespEphemeralStore`(Valkey · Garnet). 끝점은 `host:port[?prefix=game1:&timeoutMs=2000&tls=1&ca=<신뢰 PEM 파일>]`.
 *            host 는 IPv4 글 또는 `localhost` 다(Core 에 이름 해석이 없다). 비밀은 `user:password` 또는 비밀번호만.
 *          - 그 밖의 이름 · 깨진 끝점은 분명한 오류로 기동을 멈춘다(다른 드라이버로 바꾸지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IEphemeralStore;

    /** @brief RESP 끝점 글을 읽은 것입니다. */
    struct CacheEndpoint
    {
        string     _host{};
        string     _keyPrefix{};
        string     _trustFile{}; ///< `ca=` — TLS 신뢰 PEM(비면 Dev 개발용 인증서)
        NetAddress _address{};
        int64      _timeoutMs{ 2000 };
        uint8      _bTls{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CacheStoreFactory
     * @brief 캐시 공장입니다.
     */
    struct SW_GF_API CacheStoreFactory
    {
        static constexpr const utf8* kMemoryDriverName = "memory";
        static constexpr const utf8* kRespDriverName   = "resp";
        static constexpr uint16      kDefaultRespPort  = 6379;

        /**
         * @brief 앞을 만들고 띄웁니다. 실패하면 nullptr 과 까닭입니다.
         * @param secret AUTH 비밀(`ServerSecret::read` 로 읽은 것) — 쓰고 나면 부르는 쪽이 비운다
         */
        static unique_ptr<IEphemeralStore> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError );
        /** @brief 끝점 글을 읽습니다. 모르는 인자 · 깨진 값이면 false 와 까닭입니다. */
        [[nodiscard]] static bool parseEndpoint( string_view endpoint, CacheEndpoint& outEndpoint, string& outError );
    };
} // namespace sw
