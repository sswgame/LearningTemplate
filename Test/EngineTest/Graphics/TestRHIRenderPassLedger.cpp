#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHIRenderPassLedger.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// DX11 · DX12 · GL 이 같이 쓰는 렌더 패스 핸들 장부(RHIRenderPassLedger)의 규칙을 GPU 없이 본다.

/**
 * @brief [RHIRenderPassLedgerTest] 핸들은 1 부터 차례로 나가고, 없는 핸들을 놓아도 아무 일이 없다
 */
SW_TEST_CASE( RHIRenderPassLedgerTest, HandlesAreSequentialAndRetiringAnUnknownHandleIsIgnored )
{
    sw::RHIRenderPassLedger     ledger;
    const sw::RHIRenderPassDesc desc{};
    SW_EXPECT_EQUAL( static_cast<sw::RHIRenderPassHandle>( 1 ), ledger.add( desc ) );
    SW_EXPECT_EQUAL( static_cast<sw::RHIRenderPassHandle>( 2 ), ledger.add( desc ) );
    ledger.retire( 0 );
    ledger.retire( 99 ); // 등록한 적 없는 핸들은 무시한다
    ledger.retire( 1 );
    SW_EXPECT_EQUAL( static_cast<sw::RHIRenderPassHandle>( 3 ), ledger.add( desc ) ); // 놓은 핸들은 다시 나가지 않는다
    ledger.clear();
    SW_EXPECT_EQUAL( static_cast<sw::RHIRenderPassHandle>( 1 ), ledger.add( desc ) ); // 비우면 다시 1 부터다(장치 종료 뒤 재초기화)
}

/**
 * @brief [RHIRenderPassLedgerTest] 여러 스레드가 동시에 등록해도 핸들이 겹치지 않고 빠지지도 않는다
 * @details 장부는 어느 스레드에서나 부르는 계약이다(백엔드의 `createRenderPass`). 락이 없으면 벡터 재할당이 다른 등록과 겹쳐 같은 핸들이 나가거나 기록이 깨진다.
 */
SW_TEST_CASE( RHIRenderPassLedgerTest, ConcurrentAddsNeverShareAHandle )
{
    constexpr uint32 kThreadCount   = 8;
    constexpr uint32 kAddsPerThread = 256;

    sw::RHIRenderPassLedger             ledger;
    const sw::RHIRenderPassDesc         desc{};
    sw::vector<sw::RHIRenderPassHandle> listHandle( static_cast<size_t>( kThreadCount ) * kAddsPerThread );
    sw::vector<std::thread>             listThread;
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        sw::RHIRenderPassHandle* const pHandle = listHandle.data(); // 스레드마다 겹치지 않는 칸에 쓴다 - 벡터 연산자는 쓰기 감시가 같은 벡터의 동시 쓰기로 읽는다
        listThread.emplace_back( [&ledger, pHandle, &desc, threadIndex]()
        {
            for ( uint32 repeat = 0; repeat < kAddsPerThread; ++repeat )
            {
                pHandle[static_cast<size_t>( threadIndex ) * kAddsPerThread + repeat] = ledger.add( desc );
            }
        } );
    }
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }

    std::sort( listHandle.begin(), listHandle.end() );
    bool bSequential = true;
    for ( size_t index = 0; index < listHandle.size(); ++index )
    {
        bSequential = bSequential && listHandle[index] == index + 1;
    }
    SW_EXPECT_TRUE( bSequential ); // 1 부터 빠짐없이, 겹침 없이 나갔다
}
