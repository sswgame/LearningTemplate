/**
 * @file IServiceStoreDriver.h
 * @brief 서버 조립이 서버 설정의 저장소 항목(드라이버 이름)으로 실제 저장소를 만드는 인터페이스입니다. 드라이버는 저장 키트 `GF_Server_SQLStore` 의 모듈 서브시스템이 들고, 서버 조립은 타입으로 찾는다(`game::findSubsystem<IServiceStoreDriver>()`, 언리얼 `IModularFeatures` 자리). 캐시 쪽 짝은 `ICacheStoreDriver`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    class ServiceStore;

    /**
     * @class IServiceStoreDriver
     * @brief 저장소 드라이버입니다(저장 키트 `GF_Server_SQLStore` 의 모듈 서브시스템이 멤버로 들고 공개한다).
     */
    class IServiceStoreDriver
    {
    public:
        virtual ~IServiceStoreDriver() = default;

        /** @brief 저장소를 만들고 초기화합니다. 실패하면 까닭입니다(대상에 비밀을 담지 않는다). @p secret 은 쓰고 나면 부른 쪽이 비웁니다. */
        virtual expected<unique_ptr<ServiceStore>> createServiceStore( string_view driverName, string_view connection, string_view secret, int32 workerCount,
                                                                       UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw
