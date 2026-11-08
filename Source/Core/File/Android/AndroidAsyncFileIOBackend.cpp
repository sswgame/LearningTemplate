/**
 * @file AndroidAsyncFileIOBackend.cpp
 * @brief Android 에는 커널 비동기 읽기 백엔드가 없다 — Auto 는 스레드 풀 백엔드(AsyncFileIO::initialize)를 쓴다.
 * @details Android 는 seccomp · SELinux 가 io_uring 을 막는다(언리얼 Android 도 스레드 풀 읽기).
 */
#include "pch.h"

#include "Core/File/AsyncFileIOBackend.h"

#if defined( SW_PLATFORM_ANDROID )
namespace sw
{
    unique_ptr<IAsyncFileIOBackend> IAsyncFileIOBackend::createPlatform( AsyncFileIOQueue& queue, AsyncIOBackendKind kind, uint32 maxInFlightCount )
    {
        (void)queue;
        (void)kind;
        (void)maxInFlightCount;
        return nullptr;
    }
} // namespace sw
#endif
