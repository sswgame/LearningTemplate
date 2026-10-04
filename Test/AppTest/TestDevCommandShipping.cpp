#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Utility/Console/DevCommandRegistry.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief `DevCommandRegistry::getImageMarker` 와 같은 글입니다. Shipping 에서는 그 함수가 없어 여기 적습니다. */
    constexpr const utf8* kRegistryImageMarker = "sw-dev-command-registry-image-marker";

    /** @brief 파일 바이트에 글이 들어 있으면 true 입니다. */
    bool containsText( const vector<uint8>& bytes, const utf8* pText )
    {
        const string_view text{ pText };
        if ( bytes.size() < text.size() )
            return false;
        const utf8* const pBegin = reinterpret_cast<const utf8*>( bytes.data() );
        return string_view( pBegin, bytes.size() ).find( text ) != string_view::npos;
    }
} // namespace

/**
 * @brief [DevCommandShippingTest] 개발 명령 등록부는 Dev 이미지(Engine.dll)에 있고 Shipping 실행 파일에는 없다
 * @details `SW_DEV_COMMAND` · 등록부 · 콘솔은 Shipping 에서 통째로 빠져야 한다(치트가 배포본에 남으면 안 된다). 컴파일 스위치가 맞는지는
 *          바이너리를 훑어 본다 — 등록부 .cpp 의 표식 글이 Dev 의 Engine.dll 에는 있고(이 시험이 글을 찾을 수 있다는 증거), Shipping 의 App.exe
 *          에는 없어야 한다. 작업 폴더는 Bin 이다.
 */
SW_TEST_CASE( DevCommandShippingTest, RegistryIsCompiledOutOfShipping )
{
#if defined( SW_SHIPPING )
    static_assert( SW_DEV_COMMANDS_ENABLED == 0, "Shipping must compile the dev command registry out" );
    const string imagePath = FileUtil::joinPath( FileUtil::getCurrentPath(), "App.exe" );
#else
    static_assert( SW_DEV_COMMANDS_ENABLED == 1, "Dev builds keep the dev command registry" );
    SW_EXPECT_STREQ( kRegistryImageMarker, DevCommandRegistry::getImageMarker() );
    const string imagePath = FileUtil::joinPath( FileUtil::getCurrentPath(), "Engine.dll" );
#endif
    if ( FileUtil::fileExists( imagePath ) == false )
        SW_TEST_SKIP( "the image is not next to the test's working directory (run from Bin)" );
    vector<uint8> bytes;
    SW_ASSERT_TRUE( FileUtil::readFile( imagePath, bytes ) );
#if defined( SW_SHIPPING )
    SW_EXPECT_FALSE_MSG( containsText( bytes, kRegistryImageMarker ), "the Shipping App.exe still carries the dev command registry" );
#else
    SW_EXPECT_TRUE_MSG( containsText( bytes, kRegistryImageMarker ), "Engine.dll has no dev command registry - the marker search is blind" );
#endif
}
