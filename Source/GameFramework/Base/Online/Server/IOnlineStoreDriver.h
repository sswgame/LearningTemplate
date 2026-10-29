/**
 * @file IOnlineStoreDriver.h
 * @brief 서버 조립이 서버 설정의 저장소와 캐시 항목(드라이버 이름)으로 실제 드라이버를 만드는 인터페이스 둘입니다.
 * @details - 드라이버(SQLite, PostgreSQL, RESP)는 저장 키트(`GF_Server_SQLStore`, `GF_Server_CacheStore`)에 있고, 기반은 키트를 모릅니다.
 *            그래서 저장 키트가 이 인터페이스를 구현한 모듈 서브시스템을 두고(`SW_MODULE_SUBSYSTEM( Engine, …, IServiceStoreDriver )`), 서버 조립은 그 타입으로 찾습니다
 *            (`game::findSubsystem<IServiceStoreDriver>()`). 키트가 실리면 키트의 자식 범위에 서고 내려가면 범위째 사라져 "실렸는데 묶지 않았다" 나
 *            "풀지 않아 언로드된 함수를 부른다" 가 생기지 않습니다. 예전에는 기반의 원자 함수 칸 둘(`OnlineStoreDriverTable`)에 키트의 정적 객체가 묶었다.
 *          - `memory` 드라이버는 드라이버와 상관없이 서버 조립이 직접 만듭니다(그 런타임 안에서만 나눠 쓰는 메모리 구현).
 *          - 찾은 것이 없는데 다른 드라이버를 고르면 서버 조립은 기동을 멈춥니다. 다른 드라이버로 대신하지 않습니다.
 *          - 언리얼 `IModularFeatures` 로 구현을 찾는 자리와 같다 — 이름이 아니라 타입으로 찾는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    class IEphemeralStore;
    class IServiceStore;

    /** @brief 서버 설정의 `memory` 드라이버 이름입니다(저장소 · 캐시 공통). */
    inline constexpr const utf8* kMemoryStoreDriverName = "memory";

    /**
     * @class IServiceStoreDriver
     * @brief 저장소 드라이버입니다(저장 키트 `GF_Server_SQLStore` 의 모듈 서브시스템이 구현한다).
     */
    class IServiceStoreDriver
    {
    public:
        virtual ~IServiceStoreDriver() = default;

        /** @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. @p secret 은 쓰고 나면 부른 쪽이 비웁니다. */
        virtual unique_ptr<IServiceStore> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, UnixMilliseconds nowMs,
                                                              string& outError ) = 0;
    };

    /**
     * @class IEphemeralStoreDriver
     * @brief 캐시 드라이버입니다(캐시 키트 `GF_Server_CacheStore` 의 모듈 서브시스템이 구현한다).
     */
    class IEphemeralStoreDriver
    {
    public:
        virtual ~IEphemeralStoreDriver() = default;

        /** @brief 캐시를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. */
        virtual unique_ptr<IEphemeralStore> createEphemeralStore( string_view driverName, string_view endpoint, string_view secret, string& outError ) = 0;
    };
} // namespace sw
