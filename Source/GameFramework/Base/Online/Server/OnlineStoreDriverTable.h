/**
 * @file OnlineStoreDriverTable.h
 * @brief 서버 조립이 서버 설정의 저장소 · 캐시 항목(드라이버 이름)으로 실제 드라이버를 만드는 함수 테이블입니다.
 * @details - 드라이버(SQLite · PostgreSQL · RESP)는 저장 키트(`GF_Server_SQLStore`, `GF_Server_CacheStore`)에 있고, 기반은 키트를 모릅니다.
 *            그래서 저장 키트가 모듈 로드 때 만들기 함수를 이 테이블에 묶고, 서버 조립은 이 테이블로 만듭니다.
 *          - `memory` 드라이버는 테이블과 상관없이 서버 조립이 직접 만듭니다(그 런타임 안에서만 나눠 쓰는 메모리 구현).
 *          - 묶인 함수가 없는데 다른 드라이버를 고르면 서버 조립은 기동을 멈춥니다. 다른 드라이버로 대신하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IEphemeralStore;
    class IServiceStore;

    /**
     * @struct OnlineStoreDriverTable
     * @brief 저장 드라이버 만들기 함수 테이블입니다. 함수 하나씩이며, 모듈 로드(묶기)와 언로드(풀기)에서 바뀝니다.
     */
    struct SW_GF_API OnlineStoreDriverTable
    {
        /** @brief 저장소를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. @p secret 은 쓰고 나면 부른 쪽이 비웁니다. */
        using CreateServiceStoreFunction = unique_ptr<IServiceStore> ( * )( string_view driverName, string_view connection, string_view secret, int32 workerCount,
                                                                            int64 nowMs, string& outError );
        /** @brief 캐시를 만들고 초기화합니다. 실패하면 nullptr 과 까닭입니다. */
        using CreateEphemeralStoreFunction = unique_ptr<IEphemeralStore> ( * )( string_view driverName, string_view endpoint, string_view secret, string& outError );

        static constexpr const utf8* kMemoryDriverName = "memory";

        /** @brief 저장소 만들기 함수를 묶습니다. nullptr 이면 풉니다(모듈 언로드). */
        static void                         bindServiceStoreFactory( CreateServiceStoreFunction pCreate );
        static void                         bindEphemeralStoreFactory( CreateEphemeralStoreFunction pCreate );
        static CreateServiceStoreFunction   findServiceStoreFactory();
        static CreateEphemeralStoreFunction findEphemeralStoreFactory();
    };
} // namespace sw
