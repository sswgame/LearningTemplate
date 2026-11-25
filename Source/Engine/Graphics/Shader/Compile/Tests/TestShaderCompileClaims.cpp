#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompileClaims.h"

using namespace sw;

/**
 * @brief [ShaderCompileClaimsTest] 글 하나를 맡은 스레드는 한 번에 하나이고, 다른 글은 서로 기다리지 않는다
 * @details 변이: 놓을 때 알리지 않으면 기다리던 스레드가 깨어나지 않고, 키를 보지 않으면 다른 글을 맡은 채 같은 스레드가 또 맡을 때 멈춘다.
 */
SW_TEST_CASE( ShaderCompileClaimsTest, OneThreadHoldsATextAtATime )
{
    ShaderCompileClaims claims;
    atomic<uint32>      insideCount{ 0 };
    atomic<uint32>      maxInsideCount{ 0 };
    const auto          body = [&claims, &insideCount, &maxInsideCount]()
    {
        for ( int32 repeat = 0; repeat < 100; ++repeat )
        {
            const ShaderCompileClaims::Scope scope{ claims, 1 };
            const ShaderCompileClaims::Scope other{ claims, 2 };
            const uint32                     inside = insideCount.fetch_add( 1 ) + 1;
            if ( inside > 1 )
                maxInsideCount.store( inside );
            insideCount.fetch_sub( 1 );
        }
    };
    thread first{ ThreadTestAccess::getKey(), body };
    thread second{ ThreadTestAccess::getKey(), body };
    first.join();
    second.join();
    SW_EXPECT_EQUAL( 0u, maxInsideCount.load() );
}
