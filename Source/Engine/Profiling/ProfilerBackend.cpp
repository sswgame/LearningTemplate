/**
 * @file ProfilerBackend.cpp
 * @brief 활성 외부 프로파일러 출력 · 계측 지점 저장소 · 메모리 관찰 연결입니다.
 */
#include "pch.h"

#include "Engine/Profiling/ProfilerBackend.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/Tracy/TracyProfilerBackend.h"

namespace sw
{
    SW_LOG_CALLER( "Profiler" );

    namespace
    {
        struct ProfilerBackendInternal
        {
            /** @brief 활성 출력입니다. 계측 지점마다 relaxed 읽기 하나입니다. */
            static atomic<IProfilerBackend*> s_pActive;
            /** @brief Tracy 출력입니다. **프로세스 수명**이다 — 엔진이 내려간 뒤에 닫히는 구간도 이것을 부른다. */
            static TracyProfilerBackend s_tracy;

            /** @brief 지점 등록 · 이름 복사를 지킵니다. 지점마다 한 번뿐이라 잠금이 비용이 되지 않습니다. */
            static mutex           s_siteMutex;
            static ProfileZoneSite s_arrSite[ProfilerBackend::kMaxZoneSite];
            static uint32          s_siteCount;
            static utf8            s_arrNameArena[ProfilerBackend::kNameArenaBytes];
            static uint32          s_nameArenaUsed;

            /**
             * @brief 이름을 저장소에 복사합니다. 같은 문자열이 이미 있으면 그 주소입니다(`s_siteMutex` 를 잡고 부른다).
             * @details 같은 문자열 찾기는 저장소 전체를 훑는다 — 지점 수백 개 · 한 번씩이라 문제가 되지 않는다.
             */
            static const utf8* internLocked( const utf8* pText )
            {
                // 없는 문자열은 빈 문자열로 둔다 — 출력(Tracy)은 함수 · 파일 칸을 nullptr 검사 없이 문자열로 읽는다.
                if ( pText == nullptr )
                    pText = "";
                const size_t length = std::strlen( pText );
                uint32       cursor = 0;
                while ( cursor < s_nameArenaUsed )
                {
                    const utf8*  pExisting      = &s_arrNameArena[cursor];
                    const size_t existingLength = std::strlen( pExisting );
                    if ( existingLength == length && std::memcmp( pExisting, pText, length ) == 0 )
                        return pExisting;
                    cursor += static_cast<uint32>( existingLength ) + 1u;
                }
                if ( s_nameArenaUsed + length + 1u > ProfilerBackend::kNameArenaBytes )
                    return nullptr;
                utf8* pCopy = &s_arrNameArena[s_nameArenaUsed];
                Memory::copy( pCopy, pText, length );
                pCopy[length] = '\0';
                s_nameArenaUsed += static_cast<uint32>( length ) + 1u;
                return pCopy;
            }

            /** @brief 파일 경로를 `Source/` 부터 자릅니다. 절대 경로는 기계마다 다르고 길다. 없으면 그대로입니다. */
            static const utf8* trimSourcePath( const utf8* pFile )
            {
                if ( pFile == nullptr )
                    return nullptr;
                for ( const utf8* pCursor = pFile; *pCursor != '\0'; ++pCursor )
                {
                    const bool bSeparator = ( pCursor == pFile ) || pCursor[-1] == '/' || pCursor[-1] == '\\';
                    if ( bSeparator && std::strncmp( pCursor, "Source", 6 ) == 0 && ( pCursor[6] == '/' || pCursor[6] == '\\' ) )
                        return pCursor;
                }
                return pFile;
            }

#if SW_PROFILER_BACKEND_COMPILED
            /** @brief Core 할당 관찰 → 활성 출력. 받는 쪽은 할당하지 않는다(Tracy 는 자기 할당기를 쓴다). */
            static void onAllocateObserved( const void* pPtr, size_t size, MemoryTag tag )
            {
                IProfilerBackend* pBackend = s_pActive.load( std::memory_order_acquire );
                if ( pBackend != nullptr )
                    pBackend->onAllocate( pPtr, size, MemoryProfiler::getMemoryTagName( tag ) );
            }

            static void onFreeObserved( const void* pPtr, MemoryTag tag )
            {
                IProfilerBackend* pBackend = s_pActive.load( std::memory_order_acquire );
                if ( pBackend != nullptr )
                    pBackend->onFree( pPtr, MemoryProfiler::getMemoryTagName( tag ) );
            }

            static constexpr MemoryAllocationObserver kObserver{ &onAllocateObserved, &onFreeObserved };

            /** @brief 태스크 구간 지점 캐시의 칸 수입니다(스레드마다). 태스크 이름은 수십 가지다 — 겹치면 그 칸을 덮어쓴다. */
            static constexpr uint32 kTaskSiteCacheSize = 64;

            /**
             * @brief 태스크 · 스테이지 이름으로 계측 지점을 찾습니다.
             * @details 지점 등록(`registerZoneSite`)은 잠금 + 선형 검색이라 태스크마다 부르면 워커끼리 다툰다 — 스레드마다 작은 표를 앞에 둔다.
             *          대기 구간은 "Wait <이름>" 으로 등록한다(뷰어에서 실행과 갈린다).
             */
            static const ProfileZoneSite* findTaskZoneSite( const utf8* pName, TaskProfileZoneKind kind )
            {
                struct SiteCacheEntry
                {
                    uint64                 _key{ 0 };
                    const ProfileZoneSite* _pSite{ nullptr };
                };
                thread_local SiteCacheEntry t_arrSiteCache[kTaskSiteCacheSize]{};

                const uint64    key   = StringUtil::computeHash64( pName, StringUtil::strlen( pName ), false, static_cast<uint64>( kind ) + 1 ) | 1;
                SiteCacheEntry& entry = t_arrSiteCache[key % kTaskSiteCacheSize];
                if ( entry._key == key )
                    return entry._pSite;

                fixed_string<constant::kMaxBuffer64> zoneName;
                if ( kind == TaskProfileZoneKind::WaitStage )
                    zoneName.append( "Wait " );
                zoneName.append( pName );
                entry._key   = key;
                entry._pSite = ProfilerBackend::registerZoneSite( zoneName.c_str(), kind == TaskProfileZoneKind::Execute ? "TaskManager::executeTask" : "TaskManager::waitStage",
                                                                  "Source/Core/Task/TaskManager.cpp", static_cast<uint32>( kind ) );
                return entry._pSite;
            }

            /** @brief 활성 출력에 태스크 · 스테이지 구간을 엽니다. 출력이 없거나 지점 저장소가 찼으면 열지 않습니다. */
            static TaskProfileZone beginTaskZone( const utf8* pName, TaskProfileZoneKind kind )
            {
                IProfilerBackend* pBackend = s_pActive.load( std::memory_order_acquire );
                if ( pBackend == nullptr )
                    return {};
                const ProfileZoneSite* pSite = findTaskZoneSite( pName, kind );
                if ( pSite == nullptr )
                    return {};
                return TaskProfileZone{ pBackend, pBackend->beginZone( *pSite ) };
            }

            /** @brief 구간은 연 출력으로 닫는다 — 열린 동안 활성 출력이 바뀌어도 짝이 어긋나지 않는다(`ScopedFrameProfile` 과 같은 규칙). */
            static void endTaskZone( const TaskProfileZone& zone ) { static_cast<IProfilerBackend*>( zone._pContext )->endZone( zone._token ); }

            static constexpr TaskProfileHook kTaskProfileHook{ &beginTaskZone, &endTaskZone };
#endif
        };

        atomic<IProfilerBackend*> ProfilerBackendInternal::s_pActive{ nullptr };
        TracyProfilerBackend      ProfilerBackendInternal::s_tracy{};
        mutex                     ProfilerBackendInternal::s_siteMutex{};
        ProfileZoneSite           ProfilerBackendInternal::s_arrSite[ProfilerBackend::kMaxZoneSite]{};
        uint32                    ProfilerBackendInternal::s_siteCount{ 0 };
        utf8                      ProfilerBackendInternal::s_arrNameArena[ProfilerBackend::kNameArenaBytes]{};
        uint32                    ProfilerBackendInternal::s_nameArenaUsed{ 0 };
    } // namespace

    /**
     * @brief `-gv_tracy=1`: 기동부터 Tracy 로 계측을 내보냅니다(뷰어는 `tracy-profiler.exe -a 127.0.0.1` 로 붙는다).
     * @details 끄면(기본) TracyClient.dll 을 올리지도 않는다. 에디터 프로파일러 패널의 "Tracy 열기" 는 실행 중에 켠다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tracy, false, "Tracy 프로파일러로 계측을 내보낸다(기동부터, 뷰어는 localhost 로 붙는다)" );
    /**
     * @brief `-gv_tracyMemory=1`: Tracy 에 할당 · 해제를 메모리 태그별로 보냅니다. 할당마다 큐 기록이 붙어 느려집니다.
     * @details Tracy 를 켜기 **전에** 잡힌 블록의 해제는 보내지 않습니다(짝 없는 해제는 뷰어가 기록을 멈춘다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tracyMemory, false, "Tracy 에 할당 · 해제를 메모리 태그별로 보낸다(느림, gv_tracy 와 같이)" );

    bool ProfilerBackend::initialize()
    {
#if SW_PROFILER_BACKEND_COMPILED
        if ( gv_tracy == false )
            return false;
        return startTracy();
#else
        return false;
#endif
    }

    void ProfilerBackend::shutdown()
    {
        // 무엇이 나갔는지 한 줄로 남긴다 — 뷰어 없이 돌린 실행에서도 경로가 살아 있었는지 로그로 보인다.
        if ( getActiveBackend() == &ProfilerBackendInternal::s_tracy )
        {
            SW_LOG_INFO( "Tracy profiler: %# CPU zones, %# GPU zones emitted (viewer %#)", ProfilerBackendInternal::s_tracy.getZoneCount(),
                         ProfilerBackendInternal::s_tracy.getGPUZoneCount(), ProfilerBackendInternal::s_tracy.isViewerConnected() ? "connected" : "not connected" );
        }
        Memory::setAllocationObserver( nullptr );
#if SW_PROFILER_BACKEND_COMPILED
        TaskManager::setProfileHook( nullptr );
#endif
        setActiveBackend( nullptr );
    }

    bool ProfilerBackend::startTracy()
    {
#if SW_PROFILER_BACKEND_COMPILED
        if ( TracyProfilerBackend::isCompiled() == false )
        {
            SW_LOG_WARNING( "Tracy is not linked into this build (SW_ENABLE_TRACY=OFF)" );
            return false;
        }
        if ( getActiveBackend() == &ProfilerBackendInternal::s_tracy )
            return true;

        // 첫 Tracy 호출이 TracyClient.dll 을 올리고 수집 스레드 · 리슨 소켓을 세운다(지연 로드).
        setActiveBackend( &ProfilerBackendInternal::s_tracy );
        // 태스크 · 스테이지 이름이 구간이 된다(Core 는 프로파일러를 모르므로 여기서 꽂는다).
        TaskManager::setProfileHook( &ProfilerBackendInternal::kTaskProfileHook );
        if ( gv_tracyMemory )
            Memory::setAllocationObserver( &ProfilerBackendInternal::kObserver );

        // 카운터 그래프 · GPU 타임스탬프는 엔진 프로파일러 경로를 탄다. 켜 두지 않으면 Tracy 에 GPU 줄이 없다.
        if ( engine::areEngineServicesBound() )
            engine::getFrameProfiler().setEnabled( true );

        SW_LOG_INFO( "Tracy profiler started (data port %#, memory %#) — connect with tracy-profiler -a 127.0.0.1", getTracyPort(),
                     gv_tracyMemory ? "on" : "off" );
        return true;
#else
        return false;
#endif
    }

    bool ProfilerBackend::isTracyCompiled()
    {
#if SW_PROFILER_BACKEND_COMPILED
        return TracyProfilerBackend::isCompiled();
#else
        return false;
#endif
    }

    uint16 ProfilerBackend::getTracyPort() { return TracyProfilerBackend::getDataPort(); }

#if SW_PROFILER_BACKEND_COMPILED
    const TaskProfileHook* ProfilerBackend::getTaskProfileHook()
    {
        return &ProfilerBackendInternal::kTaskProfileHook;
    }
#endif

    IProfilerBackend* ProfilerBackend::getActiveBackend() { return ProfilerBackendInternal::s_pActive.load( std::memory_order_acquire ); }

    void ProfilerBackend::setActiveBackend( IProfilerBackend* pBackend )
    {
        ProfilerBackendInternal::s_pActive.store( pBackend, std::memory_order_release );
    }

    const ProfileZoneSite* ProfilerBackend::registerZoneSite( const utf8* pName, const utf8* pFunction, const utf8* pFile, uint32 line )
    {
        if ( pName == nullptr )
            return nullptr;

        const utf8*             pTrimmedFile = ProfilerBackendInternal::trimSourcePath( pFile );
        std::scoped_lock<mutex> lock{ ProfilerBackendInternal::s_siteMutex };
        for ( uint32 index = 0; index < ProfilerBackendInternal::s_siteCount; ++index )
        {
            const ProfileZoneSite& site      = ProfilerBackendInternal::s_arrSite[index];
            const bool             bSameFile = ( site._pFile == nullptr && pTrimmedFile == nullptr ) ||
                                   ( site._pFile != nullptr && pTrimmedFile != nullptr && std::strcmp( site._pFile, pTrimmedFile ) == 0 );
            const bool bSameEntry = site._line == line && bSameFile && std::strcmp( site._pName, pName ) == 0;
            if ( bSameEntry )
                return &site;
        }
        if ( ProfilerBackendInternal::s_siteCount >= kMaxZoneSite )
            return nullptr;

        const utf8* pStableName = ProfilerBackendInternal::internLocked( pName );
        if ( pStableName == nullptr )
            return nullptr;

        ProfileZoneSite& site = ProfilerBackendInternal::s_arrSite[ProfilerBackendInternal::s_siteCount];
        site._pName           = pStableName;
        site._pFunction       = ProfilerBackendInternal::internLocked( pFunction );
        site._pFile           = ProfilerBackendInternal::internLocked( pTrimmedFile );
        site._line            = line;
        site._color           = 0;
        ++ProfilerBackendInternal::s_siteCount;
        return &site;
    }

    const utf8* ProfilerBackend::internName( const utf8* pName )
    {
        std::scoped_lock<mutex> lock{ ProfilerBackendInternal::s_siteMutex };
        return ProfilerBackendInternal::internLocked( pName );
    }
} // namespace sw
