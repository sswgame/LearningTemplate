/**
 * @file WebAsyncFileIOBackend.cpp
 * @brief Web 에는 커널 비동기 읽기 백엔드가 없다 — Auto 는 스레드 풀 백엔드(AsyncFileIO::initialize)를 쓴다.
 * @details 브라우저 파일 시스템은 동기 MEMFS · IDBFS 다 — 워커 스레드의 pread 가 답이다.
 */
#include "pch.h"

#include "Core/File/AsyncFileIOBackend.h"

#if defined( SW_PLATFORM_WEB )
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
