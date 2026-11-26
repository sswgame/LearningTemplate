/**
 * @file ICacheStoreDriver.h
 * @brief 서버 조립이 서버 설정의 캐시 항목(드라이버 이름)으로 실제 캐시를 만드는 인터페이스입니다. 드라이버는 캐시 키트 `GF_Server_CacheStore` 의 모듈 서브시스템이 들고, 서버 조립은 타입으로 찾는다. 저장소 쪽 짝은 `IServiceStoreDriver`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class ICacheStore;

    /**
     * @class ICacheStoreDriver
     * @brief 캐시 드라이버입니다(캐시 키트 `GF_Server_CacheStore` 의 모듈 서브시스템이 멤버로 들고 공개한다).
     */
    class ICacheStoreDriver
    {
    public:
        virtual ~ICacheStoreDriver() = default;

        /** @brief 캐시를 만들고 초기화합니다. 실패하면 까닭입니다(대상에 비밀을 담지 않는다). */
        virtual expected<unique_ptr<ICacheStore>> createCacheStore( string_view driverName, string_view endpoint, string_view secret ) = 0;
    };
} // namespace sw
