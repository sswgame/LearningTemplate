// RegistrationChain — 붙는 순서(나중 것이 앞), 걷기(takeAll)가 비우고 다음 이미지는 제 것만 다는 것, 떼어 둔 머리로 되돌리기.
#include "pch.h"

#include "Core/Registry/RegistrationChain.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 사슬에 붙는 등록자 역할입니다. 정적 등록자와 같이 생성자에서 붙는다. */
    struct ChainProbe
    {
        int32       _value;
        ChainProbe* _pNext;

        ChainProbe( RegistrationChain<ChainProbe>& chain, int32 value )
            : _value{ value }
            , _pNext{ chain.pushFront( this ) }
        {
        }
    };
} // namespace

/** @brief [RegistrationChainTest] 나중에 붙은 것이 앞이고, 걷으면 머리가 비어 다음에 붙는 것은 걷어 간 것과 섞이지 않는다 */
SW_TEST_CASE( RegistrationChainTest, PushFrontOrderAndTakeAllEmpties )
{
    RegistrationChain<ChainProbe> chain;
    SW_EXPECT_TRUE( chain.getHead() == nullptr );

    ChainProbe first{ chain, 1 };
    ChainProbe second{ chain, 2 };
    SW_REQUIRE_TRUE( chain.getHead() == &second );
    SW_EXPECT_TRUE( second._pNext == &first );
    SW_EXPECT_TRUE( first._pNext == nullptr );

    ChainProbe* const pTaken = chain.takeAll();
    SW_EXPECT_TRUE( pTaken == &second );
    SW_EXPECT_TRUE( chain.getHead() == nullptr );

    // 걷은 뒤 붙는 것(다음 이미지)은 제 사슬만 이룬다
    ChainProbe third{ chain, 3 };
    SW_EXPECT_TRUE( chain.getHead() == &third );
    SW_EXPECT_TRUE( third._pNext == nullptr );
}

/** @brief [RegistrationChainTest] 떼어 둔 머리로 되돌리면(중단한 핫 리로드) 그 사이에 붙은 것은 사슬에서 빠진다 */
SW_TEST_CASE( RegistrationChainTest, ResetHeadRestoresSavedChain )
{
    RegistrationChain<ChainProbe> chain;
    ChainProbe                    kept{ chain, 1 };
    ChainProbe* const             pSaved = chain.getHead();

    ChainProbe transient{ chain, 2 };
    SW_EXPECT_TRUE( chain.getHead() == &transient );

    chain.resetHead( pSaved );
    SW_EXPECT_TRUE( chain.getHead() == &kept );
    SW_EXPECT_TRUE( kept._pNext == nullptr );
}
