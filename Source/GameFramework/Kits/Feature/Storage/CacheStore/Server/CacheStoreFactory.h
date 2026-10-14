/**
 * @file CacheStoreFactory.h
 * @brief 서버 설정의 캐시 항목(`ServerCacheEntry`) 하나로 휘발성 저장소를 만듭니다. 캐시 항목은 드라이버 이름, 엔드포인트, 비밀로 이루어집니다.
 * @details - "memory": 기반 계층의 메모리 구현이다. 서버 한 대나 개발용이며, 같은 프로세스의 저장소끼리 데이터를 나눠 쓴다.
 *          - "resp": `RespEphemeralStore`(Valkey, Garnet)이다. 엔드포인트는 `host:port[?prefix=game1:&timeoutMs=2000&tls=1&ca=<신뢰 PEM 파일>]` 형식이다.
 *            host 는 IPv4 문자열이나 `localhost` 이다. Core 에 이름 해석이 없기 때문이다. 비밀은 `user:password` 또는 비밀번호만 쓴다.
 *          - 그 밖의 이름이나 잘못된 엔드포인트는 분명한 오류를 남기고 기동을 멈춘다. 다른 드라이버로 대신하지 않는다.
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

    /** @brief RESP 엔드포인트 문자열을 해석한 결과입니다. */
    struct CacheEndpoint
    {
        string     _host{};
        string     _keyPrefix{};
        string     _trustFile{}; ///< `ca=` 값이다. TLS 신뢰 PEM 이며 비면 Dev 개발용 인증서를 쓴다
        NetAddress _address{};
        int64      _timeoutMs{ 2000 };
        uint8      _bTLS{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CacheStoreFactory
     * @brief 캐시 팩토리입니다.
     */
    struct SW_KIT_API CacheStoreFactory
    {
        static constexpr const utf8* kMemoryDriverName = "memory";
        static constexpr const utf8* kRespDriverName   = "resp";
        static constexpr uint16      kDefaultRespPort  = 6379;

        /**
         * @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param secret AUTH 비밀(`ServerSecret::read` 로 읽은 것)입니다. 쓰고 나면 호출한 쪽이 비웁니다.
         */
        static unique_ptr<IEphemeralStore> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError );
        /** @brief 엔드포인트 문자열을 해석합니다. 모르는 인자이거나 값이 깨졌으면 false 와 실패 이유를 반환합니다. */
        [[nodiscard]] static bool parseEndpoint( string_view endpoint, CacheEndpoint& outEndpoint, string& outError );
    };
} // namespace sw
