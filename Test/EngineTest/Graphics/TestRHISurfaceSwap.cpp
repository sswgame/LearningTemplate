#include "pch.h"

#include "Engine/Graphics/RHI/IRenderSurface.h"
#include "Engine/Graphics/RHI/RHI.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief recreateSurface 호출 수를 세고 정해 둔 결과를 돌려주는 시험 표면입니다. */
    class CountingSurface final : public sw::IRenderSurface
    {
    public:
        explicit CountingSurface( bool bRecreateSucceeds )
            : _recreateCount{ 0 }
            , _bRecreateSucceeds{ bRecreateSucceeds }
        {
        }

        void*  getSurfaceHandle() const override { return nullptr; }
        void*  getSurfaceDisplay() const override { return nullptr; }
        uint32 getSurfaceWidth() const override { return 0; }
        uint32 getSurfaceHeight() const override { return 0; }

        [[nodiscard]] bool recreateSurface() override
        {
            ++_recreateCount;
            return _bRecreateSucceeds;
        }

        uint32 _recreateCount;
        bool   _bRecreateSucceeds;
    };
} // namespace

/**
 * @brief [RHISurfaceSwapTest] 창 재생성이 필요한 교체에서 재생성이 실패하면 교체가 실패한다
 * @details WGL 픽셀 포맷은 창에 한 번만 붙어, OpenGL 이 낀 교체는 창을 다시 만들어야 새 디바이스가 그 창을 쓸 수 있다.
 *          재생성 결과를 버리면 실패한 창 위에 새 디바이스를 만들고 교체가 성공한 것처럼 보인다.
 */
SW_TEST_CASE( RHISurfaceSwapTest, FailedRecreateFailsTheSwapThatNeedsIt )
{
    CountingSurface surface( false );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_FALSE( sw::RHI::recreateSurfaceForSwap( &surface, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL ) );
    SW_EXPECT_FALSE( sw::RHI::recreateSurfaceForSwap( &surface, sw::RHIBackend::OpenGL, sw::RHIBackend::Vulkan ) );
    SW_EXPECT_EQUAL( 2u, surface._recreateCount );
#else
    SW_EXPECT_TRUE( sw::RHI::recreateSurfaceForSwap( &surface, sw::RHIBackend::Vulkan, sw::RHIBackend::OpenGL ) );
    SW_EXPECT_EQUAL( 0u, surface._recreateCount );
#endif
}

/**
 * @brief [RHISurfaceSwapTest] 창 재생성이 필요 없는 교체는 표면을 건드리지 않는다
 */
SW_TEST_CASE( RHISurfaceSwapTest, SwapWithoutWindowRequirementLeavesTheSurface )
{
    CountingSurface surface( false );
    SW_EXPECT_TRUE( sw::RHI::recreateSurfaceForSwap( &surface, sw::RHIBackend::DirectX12, sw::RHIBackend::DirectX11 ) );
    SW_EXPECT_TRUE( sw::RHI::recreateSurfaceForSwap( &surface, sw::RHIBackend::Vulkan, sw::RHIBackend::DirectX12 ) );
    SW_EXPECT_TRUE( sw::RHI::recreateSurfaceForSwap( nullptr, sw::RHIBackend::DirectX12, sw::RHIBackend::OpenGL ) );
    SW_EXPECT_EQUAL( 0u, surface._recreateCount );
}
