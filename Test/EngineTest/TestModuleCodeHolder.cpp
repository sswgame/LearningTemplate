#include "pch.h"

#include "Core/Delegate/ModuleCodeHolder.h"

#include "Engine/Module/ModuleTypeRegistry.h"

#include "TestFramework/TestFramework.h"

// ModuleCodeHolderTest — 모듈 이미지를 내리기 전의 정리가 훑는 보유자 목록(`IModuleCodeHolder`).

#if !defined( SW_SHIPPING )
namespace
{
    /** @brief "모듈 이미지" 로 삼는 바이트 범위입니다. 다른 보유자의 코드 주소가 여기 들 일은 없습니다. */
    uint8 s_arrProbeImage[16]{};

    /** @brief 코드 주소 하나를 들고, 범위 안이면 떼는 가짜 보유자입니다. */
    class ProbeCodeHolder final : public sw::IModuleCodeHolder
    {
    public:
        ProbeCodeHolder()
            : _pCode{ nullptr }
            , _bKeepImageMapped{ false }
        {
        }

        const utf8* getModuleCodeHolderName() const override { return "probe entries"; }

        uint32 releaseModuleCodeWithin( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
        {
            if ( isAddressWithin( _pCode, pBegin, pEnd ) == false )
                return 0;
            if ( _bKeepImageMapped )
            {
                outKeepImageMapped = true;
                return 0;
            }
            _pCode = nullptr;
            return 1;
        }

        const void* _pCode;
        bool        _bKeepImageMapped;
    };
} // namespace

/**
 * @brief [ModuleCodeHolderTest] 정리는 살아 있는 보유자를 모두 훑는다 — 만들어지면 들고, 사라지면 빠진다
 * @details 등록부를 하나 더할 때 고칠 곳은 그 등록부의 상속 한 줄이어야 한다. 여기서는 엔진이 모르는 가짜 보유자를 세워 `releaseModuleCode` 가
 *          그것까지 지나가는지, 범위 밖은 건드리지 않는지, "이미지를 내리지 말라" 는 답을 그대로 올리는지, 사라진 보유자는 목록에서 빠지는지 본다.
 */
SW_TEST_CASE( ModuleCodeHolderTest, ReleaseModuleCodeSweepsEveryLiveHolder )
{
    SW_TEST_DEFENSIVE_SCOPE( "releaseModuleCode warns about what a probe holder kept" );
    const uint32 holderCountBefore = sw::IModuleCodeHolder::getHolderCount();
    {
        ProbeCodeHolder probe;
        SW_EXPECT_EQUAL( holderCountBefore + 1, sw::IModuleCodeHolder::getHolderCount() );

        // 범위 안: 뗀다.
        probe._pCode = &s_arrProbeImage[4];
        bool bKeepImageMapped{ true };
        SW_EXPECT_EQUAL( 1u, sw::engine::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 16, &bKeepImageMapped ) );
        SW_EXPECT_NULL( probe._pCode );
        SW_EXPECT_FALSE( bKeepImageMapped );

        // 범위 밖: 그대로다.
        probe._pCode = &s_arrProbeImage[12];
        SW_EXPECT_EQUAL( 0u, sw::engine::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 8, &bKeepImageMapped ) );
        SW_EXPECT_TRUE( probe._pCode == &s_arrProbeImage[12] );

        // 떼어 낼 수 없다는 보유자의 답은 부르는 쪽에 그대로 간다.
        probe._bKeepImageMapped = true;
        SW_EXPECT_EQUAL( 0u, sw::engine::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 16, &bKeepImageMapped ) );
        SW_EXPECT_TRUE( bKeepImageMapped );

        // 사본도 따로 오른다(복사로 생긴 등록부가 훑기에서 빠지지 않게).
        const ProbeCodeHolder copy{ probe };
        SW_EXPECT_EQUAL( holderCountBefore + 2, sw::IModuleCodeHolder::getHolderCount() );
    }
    SW_EXPECT_EQUAL( holderCountBefore, sw::IModuleCodeHolder::getHolderCount() );
}
#endif
