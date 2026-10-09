#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleBuildId.h"

#include "Engine/Module/ModuleCatalog.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kServerOnlyMarkerPrefix = "sw-server-only-module:";
#if defined( SW_PLATFORM_WINDOWS )
    /** @brief 서버 산출물의 임포트 표에 있으면 안 되는 GPU 라이브러리입니다(대소문자 무시 — 링커가 적는 대소문자는 라이브러리마다 다르다). */
    constexpr const utf8*                  kArrGraphicsLibrary[]   = { "d3d12.dll", "d3d11.dll", "dxgi.dll", "vulkan-1.dll", "opengl32.dll" };
    constexpr const utf8*                  kAppImageName           = "App.exe";
    constexpr const utf8*                  kServerImageName        = "Server.exe";
    [[maybe_unused]] constexpr const utf8* kModuleLibraryFolder    = "";
    [[maybe_unused]] constexpr const utf8* kModuleLibraryExtension = ".dll";
#else
    /** @brief 서버 산출물의 DT_NEEDED 에 있으면 안 되는 창 · GPU 라이브러리입니다 — 서버 기계에 없어 main 전에 동적 링커가 실패한다. */
    constexpr const utf8*                  kArrGraphicsLibrary[]   = { "libX11.so", "libxcb.so", "libX11-xcb.so", "libvulkan.so", "libGL.so", "libGLX.so", "libEGL.so" };
    constexpr const utf8*                  kAppImageName           = "App";
    constexpr const utf8*                  kServerImageName        = "Server";
    [[maybe_unused]] constexpr const utf8* kModuleLibraryFolder    = "Lib";
    [[maybe_unused]] constexpr const utf8* kModuleLibraryExtension = ".so";
#endif

    /** @brief @p bytes 안에 @p text 가 있는가(대소문자 무시 가능). 실행 파일 수십 MB 를 소문자로 복사하지 않고 그 자리에서 견준다. */
    bool containsText( const vector<uint8>& bytes, string_view text, bool bIgnoreCase )
    {
        const string_view haystack( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        if ( bIgnoreCase == false )
            return haystack.find( text ) != string_view::npos;
        for ( size_t index = 0; index + text.size() <= haystack.size(); ++index )
        {
            size_t matched = 0;
            while ( matched < text.size() && StringUtil::toLowerChar( haystack[index + matched] ) == StringUtil::toLowerChar( text[matched] ) )
            {
                ++matched;
            }
            if ( matched == text.size() )
                return true;
        }
        return false;
    }

    /** @brief 작업 폴더(Bin) 옆의 실행 파일 경로입니다. 없으면 빈 글. */
    string findImageInBin( const utf8* pName )
    {
        const string path = FileUtil::joinPath( FileUtil::getCurrentPath(), pName );
        return FileUtil::exists( path ) ? path : string{};
    }

    /** @brief 이 빌드의 Engine 이미지(Dev 는 Engine.dll · libEngine.so, Shipping 은 시험 실행 파일 자신)입니다. */
    string findEngineImage()
    {
        return ModuleBuildId::find( reinterpret_cast<const void*>( &ModuleCatalog::getBuildTargetMask ) )._modulePath;
    }

    /** @brief Dev 의 모듈 라이브러리(Bin 의 .dll · Bin/Lib 의 .so)입니다. Shipping 은 모듈이 정적 링크라 비어 있다. */
    vector<string> listModuleLibraries()
    {
        vector<string> listPath;
#if !defined( SW_SHIPPING )
        const string folder = FileUtil::joinPath( FileUtil::getCurrentPath(), kModuleLibraryFolder );
        (void)FileUtil::collectFiles( folder, kModuleLibraryExtension, listPath, false );
#endif
        return listPath;
    }
} // namespace

/**
 * @brief [BuildTargetImageTest] 클라이언트 타깃의 산출물(App · 모듈 라이브러리)에는 서버 전용 모듈의 표식이 없다
 * @details 서버 전용 코드(DB · 캐시 드라이버, 서비스 서버)가 플레이어 배포본에 들어가면 안 된다. 매니페스트 · 게이트(CheckModuleTargets)는 소스를
 *          보고, 이 시험은 링크 결과를 본다. Game 타깃은 둘을 일부러 다 담으니 보지 않는다.
 */
SW_TEST_CASE( BuildTargetImageTest, ClientImagesCarryNoServerOnlyModule )
{
    // 타깃은 빌드 마스크(다른 TU 의 함수)로 묻는다 — 상수 이름을 견주면 쓰지 않는 갈래가 "닿지 않는 코드" 경고가 된다.
    if ( ModuleCatalog::getBuildTargetMask() != static_cast<uint8>( ModuleTarget::Client ) )
        SW_TEST_SKIP( "only the Client target promises to carry no server code" );
    const string appImage = findImageInBin( kAppImageName );
    if ( appImage.empty() )
        SW_TEST_SKIP( "App is not next to the test's working directory (run from Bin)" );
    vector<string> listImage = listModuleLibraries();
    listImage.push_back( appImage );
    for ( const string& imagePath : listImage )
    {
        vector<uint8> bytes;
        SW_ASSERT_TRUE( FileUtil::readFile( imagePath, bytes ) );
        SW_EXPECT_FALSE_MSG( containsText( bytes, kServerOnlyMarkerPrefix, false ), imagePath.c_str() );
    }
}

/**
 * @brief [BuildTargetImageTest] 서버 실행 파일은 서버 표식을 갖고(검색이 눈멀지 않았다), 서버 타깃의 산출물은 창 · GPU 라이브러리를 링크하지 않는다
 * @details 리눅스 서버가 libX11 을 링크하면 X 없는 기계에서 main 전에 실패한다(`Linux.cmake` 의 서버 분기 · X11 파일 가드를 되돌리면 이 시험이 진다 — WSL).
 *          Windows 는 RHI 백엔드가 서버 타깃에 없으니 d3d12 · dxgi · vulkan-1 · opengl32 를 임포트하지 않는다.
 */
SW_TEST_CASE( BuildTargetImageTest, ServerImageHasTheMarkerAndNoGraphicsLibrary )
{
    if ( ( ModuleCatalog::getBuildTargetMask() & static_cast<uint8>( ModuleTarget::Server ) ) == 0 )
        SW_TEST_SKIP( "this target builds no Server executable" );
    const string serverImage = findImageInBin( kServerImageName );
    if ( serverImage.empty() )
        SW_TEST_SKIP( "Server is not next to the test's working directory (run from Bin)" );
    vector<uint8> serverBytes;
    SW_ASSERT_TRUE( FileUtil::readFile( serverImage, serverBytes ) );
    SW_EXPECT_TRUE_MSG( containsText( serverBytes, kServerOnlyMarkerPrefix, false ), "the Server image has no server marker - the marker search is blind" );

    if ( ModuleCatalog::getBuildTargetMask() != static_cast<uint8>( ModuleTarget::Server ) )
        return; // Game 타깃의 Server 는 클라이언트 코드(X11 등)를 같이 링크한다 — 개발 편의 빌드라 보지 않는다
    vector<string> listImage{ serverImage };
    // Engine 이 정적 링크(Shipping)면 그 이미지는 이 시험 실행 파일이다 — 시험 실행 파일은 GPU 시험을 위해 그래픽 라이브러리를 링크하므로 보지 않는다.
    // 그때 Engine 코드는 Server 이미지 안에 있다.
    const string engineImage = findEngineImage();
    if ( engineImage.empty() == false && engineImage != serverImage && FileUtil::pathsEqualNormalized( engineImage, FileUtil::getExecutablePath() ) == false )
        listImage.push_back( engineImage );
    for ( const string& imagePath : listImage )
    {
        vector<uint8> bytes;
        SW_ASSERT_TRUE( FileUtil::readFile( imagePath, bytes ) );
        for ( const utf8* pLibrary : kArrGraphicsLibrary )
        {
            SW_EXPECT_FALSE_MSG( containsText( bytes, pLibrary, true ), ( imagePath + " links " + pLibrary ).c_str() );
        }
    }
}
