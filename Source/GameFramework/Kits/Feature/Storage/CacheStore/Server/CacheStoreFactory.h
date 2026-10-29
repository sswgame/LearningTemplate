/**
 * @file CacheStoreFactory.h
 * @brief 서버 설정의 캐시 항목(`ServerCacheEntry`) 하나로 휘발성 저장소를 만듭니다. 캐시 항목은 드라이버 이름, 엔드포인트, 비밀로 이루어집니다.
 * @details - "memory": 기반 계층의 메모리 구현이다. 서버 한 대나 개발용이며, 같은 프로세스의 저장소끼리 데이터를 나눠 쓴다.
 *          - "resp": `RespEphemeralStore`(Valkey, Garnet)이다. 엔드포인트는 `host:port[?prefix=game1:&timeoutMs=2000&tls=1&ca=<신뢰 PEM 파일>]` 형식이다.
 *            host 는 IPv4 문자열이나 호스트 이름이다. 이름은 기동 때 한 번 운영체제 해석기로 푼다(`PlatformSocketUtil::resolveAddress`, IPv4 A 레코드).
 *            비밀은 `user:password` 또는 비밀번호만 쓴다.
 *          - 그 밖의 이름이나 잘못된 엔드포인트는 분명한 오류를 남기고 기동을 멈춘다. 다른 드라이버로 대신하지 않는다.
 *          만들기는 이 키트의 모듈 서브시스템(`IEphemeralStoreDriver` 로 공개, CacheStoreFactory.cpp)이 하고, 서버 조립은 그 타입으로 찾는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
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
     * @brief 캐시 팩토리의 드라이버 이름 · 엔드포인트 해석입니다(만들기는 키트의 모듈 서브시스템 — 파일 머리말).
     */
    struct SW_KIT_SERVER_API CacheStoreFactory
    {
        static constexpr const utf8* kRespDriverName  = "resp";
        static constexpr uint16      kDefaultRespPort = 6379;

        /** @brief 엔드포인트 문자열을 해석합니다. 호스트 이름이면 DNS 를 묻는 막는 호출입니다. 모르는 인자이거나 값이 깨졌거나 풀리지 않으면 false 와 실패 이유를 반환합니다. */
        [[nodiscard]] static bool parseEndpoint( string_view endpoint, CacheEndpoint& outEndpoint, string& outError );
    };
} // namespace sw
