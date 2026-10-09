#include "pch.h"

#include "Engine/Physics/Jolt/JoltPhysicsBackend.h"

#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Physics/Jolt/JoltJobSystem.h"
#include "Engine/Physics/Jolt/JoltPhysicsScene.h"
#include "Engine/Physics/Jolt/JoltUtil.h"
#include "Engine/Physics/PhysicsSettings.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

namespace sw
{
    SW_LOG_CALLER( "JoltPhysicsBackend" );

    namespace
    {
        struct JoltPhysicsBackendInternal
        {
            /** @brief 동시에 살아 있을 수 있는 잡 · 장벽 수입니다(Jolt 예제 값 — 씬 여럿이 함께 쓴다). */
            static constexpr uint32 kMaxJobCount     = 2048;
            static constexpr uint32 kMaxBarrierCount = 16;

            static void* allocate( size_t size ) { return Memory::allocate( size ); }
            static void* reallocate( void* pBlock, size_t oldSize, size_t newSize )
            {
                void* pNew = Memory::allocate( newSize );
                if ( pBlock != nullptr && pNew != nullptr )
                {
                    Memory::copy( pNew, pBlock, oldSize < newSize ? oldSize : newSize );
                    Memory::free( pBlock );
                }
                return pNew;
            }
            static void  free( void* pBlock ) { Memory::free( pBlock ); }
            static void* allocateAligned( size_t size, size_t alignment ) { return Memory::allocateAligned( size, alignment ); }
            static void  freeAligned( void* pBlock ) { Memory::freeAligned( pBlock ); }

            /** @brief Jolt 의 printf 꼴 추적 줄을 엔진 로그로 옮깁니다. 형식 특성을 달아 형식 문자열이 인자에서 온다는 것을 컴파일러가 안다. */
#if SW_COMPILER_CLANG || SW_COMPILER_GCC
            __attribute__( ( format( printf, 1, 2 ) ) )
#endif
            static void
            trace( const utf8* pFormat, ... )
            {
                utf8    arrMessage[constant::kMaxBuffer1024];
                va_list arguments;
                va_start( arguments, pFormat );
                ::vsnprintf( arrMessage, sizeof( arrMessage ), pFormat, arguments );
                va_end( arguments );
                SW_LOG_INFO( "%#", arrMessage );
            }

#ifdef JPH_ENABLE_ASSERTS
            static bool assertFailed( const utf8* pExpression, const utf8* pMessage, const utf8* pFile, JPH::uint line )
            {
                // `Vec3::CheckW`(W == Z) 는 부동소수 예외 설정이 켜진 헤더에서만 돈다. vcpkg 설치본의 헤더는 그것을 켜고 라이브러리는 끄고 지어
                // 라이브러리가 돌려준 벡터의 W 는 맞춰져 있지 않다 — 거짓 경보라 넘긴다(`findLibraryVersionId` 의 같은 사정).
                const string_view file{ pFile != nullptr ? pFile : "" };
                const string_view expression{ pExpression != nullptr ? pExpression : "" };
                if ( StringUtil::endsWith( file, "Vec3.inl", true ) && expression.find( "mF32)[2] == " ) != string_view::npos )
                    return false;
                SW_LOG_ERROR( "Jolt assert %# (%#) at %#:%#", pExpression, pMessage != nullptr ? pMessage : "", pFile, line );
                return false; // 중단점을 걸지 않는다 — 엔진 로그가 [Error] 로 남긴다
            }
#endif

            /** @brief 타입 팩토리 자리 — 정적 객체로 두면 프로세스 끝에 엔진 할당자가 내려간 뒤 소멸하므로, 올릴 때 만들고 내릴 때 없앤다. */
            alignas( JPH::Factory ) static uint8 s_arrFactoryStorage[sizeof( JPH::Factory )];
            /**
             * @brief 라이브러리가 받아들이는 버전 ID 를 찾습니다(`JPH::VerifyJoltVersionIDInternal`). 없으면 false 입니다.
             * @details vcpkg 포트는 공유 라이브러리 설치본의 `Core.h` 에 `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` 를 박아 넣지만 라이브러리는 그것 없이
             *          짓는다 — 그 비트 하나만 다르면 받아들인다. 그 설정은 부동소수 예외를 켜고 끄는 스택 위의 가드(`FPException.h`)만 바꾸고
             *          구조체 배치는 바꾸지 않는다. 다른 비트(단언 · 정밀도 · 디버그 렌더러 · 프로파일러 · 레이어 비트)가 다르면 거부한다.
             */
            static bool findLibraryVersionId( uint64& outVersionId )
            {
                constexpr uint64 kFloatingPointExceptionBit = static_cast<uint64>( 1 ) << ( 24 + 2 );
                const uint64     arrCandidate[2]            = { JPH_VERSION_ID, JPH_VERSION_ID ^ kFloatingPointExceptionBit };
                for ( const uint64 candidate : arrCandidate )
                {
                    if ( JPH::VerifyJoltVersionIDInternal( candidate ) )
                    {
                        outVersionId = candidate;
                        return true;
                    }
                }
                return false;
            }

            static JPH::Factory*  s_pFactory;
            static JoltJobSystem* s_pJobSystem;
            static bool           s_bInitialized;
        };

        alignas( JPH::Factory ) uint8 JoltPhysicsBackendInternal::s_arrFactoryStorage[sizeof( JPH::Factory )]{};
        JPH::Factory*  JoltPhysicsBackendInternal::s_pFactory{ nullptr };
        JoltJobSystem* JoltPhysicsBackendInternal::s_pJobSystem{ nullptr };
        bool           JoltPhysicsBackendInternal::s_bInitialized{ false };
    } // namespace
} // namespace sw

namespace sw
{
    bool JoltPhysicsBackend::initialize()
    {
        if ( JoltPhysicsBackendInternal::s_bInitialized )
            return true;
        SW_MEMORY_SCOPE( Physics );

        JPH::Allocate        = &JoltPhysicsBackendInternal::allocate;
        JPH::Reallocate      = &JoltPhysicsBackendInternal::reallocate;
        JPH::Free            = &JoltPhysicsBackendInternal::free;
        JPH::AlignedAllocate = &JoltPhysicsBackendInternal::allocateAligned;
        JPH::AlignedFree     = &JoltPhysicsBackendInternal::freeAligned;
        JPH::Trace           = &JoltPhysicsBackendInternal::trace;
        JPH_IF_ENABLE_ASSERTS( JPH::AssertFailed = &JoltPhysicsBackendInternal::assertFailed; )

        // 헤더가 본 설정(정밀도 · 단언 · 디버그 렌더러 …)과 DLL 이 지어진 설정이 다르면 구조체 배치가 어긋난다 — 쓰기 전에 멈춘다.
        uint64 libraryVersionId = 0;
        if ( JoltPhysicsBackendInternal::findLibraryVersionId( libraryVersionId ) == false )
        {
            SW_LOG_ERROR( "Jolt headers and the Jolt library were built with different settings (JPH_VERSION_ID mismatch)" );
            return false;
        }

        JoltPhysicsBackendInternal::s_pFactory = sw_placement_new( JoltPhysicsBackendInternal::s_arrFactoryStorage ) JPH::Factory();
        JPH::Factory::sInstance                = JoltPhysicsBackendInternal::s_pFactory;
        // `JPH::RegisterTypes()` 는 헤더의 버전 ID 를 넘겨 다르면 abort 한다 — 위에서 맞춘 라이브러리의 ID 로 직접 부른다.
        JPH::RegisterTypesInternal( libraryVersionId );
        JoltPhysicsBackendInternal::s_pJobSystem   = JoltUtil::createObject<JoltJobSystem>( JoltPhysicsBackendInternal::kMaxJobCount, JoltPhysicsBackendInternal::kMaxBarrierCount );
        JoltPhysicsBackendInternal::s_bInitialized = true;
        SW_LOG_INFO( "Jolt %#.%#.%# initialized (job concurrency %#)", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH,
                     JoltPhysicsBackendInternal::s_pJobSystem->GetMaxConcurrency() );
        return true;
    }

    void JoltPhysicsBackend::shutdown()
    {
        if ( JoltPhysicsBackendInternal::s_bInitialized == false )
            return;
        JoltUtil::destroyObject( JoltPhysicsBackendInternal::s_pJobSystem );
        JoltPhysicsBackendInternal::s_pJobSystem = nullptr;
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
        JoltPhysicsBackendInternal::s_pFactory->~Factory();
        JoltPhysicsBackendInternal::s_pFactory     = nullptr;
        JoltPhysicsBackendInternal::s_bInitialized = false;
    }

    bool JoltPhysicsBackend::isInitialized()
    {
        return JoltPhysicsBackendInternal::s_bInitialized;
    }

    unique_ptr<IPhysicsScene3D> JoltPhysicsBackend::createScene( const PhysicsSettings& settings )
    {
        if ( JoltPhysicsBackendInternal::s_bInitialized == false )
        {
            SW_LOG_ERROR( "createScene: the Jolt backend is not initialized (PhysicsSystem::initialize)" );
            return nullptr;
        }
        SW_MEMORY_SCOPE( Physics );
        return make_unique<JoltPhysicsScene>( settings, JoltPhysicsBackendInternal::s_pJobSystem );
    }
} // namespace sw
