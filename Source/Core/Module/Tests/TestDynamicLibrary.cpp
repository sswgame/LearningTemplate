// DynamicLibrary — 소유 형식은 이동하면 앞 값이 비고, reset · leakForProcessLifetime 뒤에는 빈 값이며, 뷰는 같은 라이브러리의 심볼을 찾는다.
#include "pch.h"

#include "Core/Module/DynamicLibrary.h"
#include "Core/TestFramework/TestFramework.h"

#include <type_traits>

using namespace sw;

static_assert( std::is_copy_constructible_v<DynamicLibrary> == false, "two owners would unload one library twice" );
static_assert( std::is_nothrow_move_constructible_v<DynamicLibrary>, "containers move the owner on growth" );
static_assert( std::is_constructible_v<DynamicLibraryView, void*> == false, "a view is made only from a loaded library, not from a raw OS value" );

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
namespace
{
    /** @brief 어느 프로세스에나 올라와 있는 시스템 라이브러리와 그 안의 함수 이름입니다. 참조 수만 오르므로 내려도 프로세스에는 남는다. */
    struct AlwaysLoadedLibrary
    {
    #if defined( SW_PLATFORM_WINDOWS )
        static constexpr const utf8* kName   = "kernel32.dll";
        static constexpr const utf8* kSymbol = "GetCurrentProcessId";
    #elif defined( SW_PLATFORM_LINUX )
        static constexpr const utf8* kName   = "libc.so.6";
        static constexpr const utf8* kSymbol = "getpid";
    #else
        #error "Unsupported platform"
    #endif
    };
} // namespace

/** @brief [DynamicLibraryTest] 이동하면 소유가 옮겨 가고 앞 값은 빈다. reset 뒤에는 빈 값이다 */
SW_TEST_CASE( DynamicLibraryTest, MoveTransfersOwnershipAndResetEmpties )
{
    DynamicLibrary first = DynamicLibrary::loadSystem( AlwaysLoadedLibrary::kName );
    SW_REQUIRE_TRUE( first.isValid() );
    const DynamicLibraryView view = first.getView();
    SW_EXPECT_TRUE( first.findSymbol( AlwaysLoadedLibrary::kSymbol ) != nullptr );

    DynamicLibrary second = std::move( first );
    SW_EXPECT_FALSE( first.isValid() ); // NOLINT(bugprone-use-after-move): 옮긴 소유는 빈다
    SW_EXPECT_TRUE( second.isValid() );
    SW_EXPECT_TRUE( second.getView() == view );

    second.reset();
    SW_EXPECT_FALSE( second.isValid() );
    SW_EXPECT_TRUE( second.findSymbol( AlwaysLoadedLibrary::kSymbol ) == nullptr );
}

/** @brief [DynamicLibraryTest] 프로세스 끝까지 둔 라이브러리는 소유가 비고 뷰가 그대로 심볼을 찾는다 */
SW_TEST_CASE( DynamicLibraryTest, LeakKeepsViewUsable )
{
    DynamicLibrary           library = DynamicLibrary::loadSystem( AlwaysLoadedLibrary::kName );
    const DynamicLibraryView kept    = library.leakForProcessLifetime();
    SW_EXPECT_FALSE( library.isValid() );
    SW_EXPECT_TRUE( kept.isValid() );
    SW_EXPECT_TRUE( kept.findSymbol( AlwaysLoadedLibrary::kSymbol ) != nullptr );
}

/** @brief [DynamicLibraryTest] 빈 값 · 빈 뷰 · 없는 라이브러리는 심볼을 찾지 않고 빈 결과다 */
SW_TEST_CASE( DynamicLibraryTest, EmptyAndMissingLibraryFindNothing )
{
    SW_EXPECT_FALSE( DynamicLibrary::loadSystem( "sw_no_such_library_for_test" ).isValid() );
    SW_EXPECT_FALSE( DynamicLibrary::loadSystem( "" ).isValid() );
    SW_EXPECT_TRUE( DynamicLibraryView{}.findSymbol( AlwaysLoadedLibrary::kSymbol ) == nullptr );
    SW_EXPECT_FALSE( DynamicLibraryView::findLoadedAndPin( "sw_no_such_library_for_test" ).isValid() );
}
#elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
// 늘 올라와 있는 시스템 라이브러리 이름을 정하지 않은 플랫폼이다 — 형식 검사(static_assert)만 본다.
#else
    #error "Unsupported platform"
#endif
