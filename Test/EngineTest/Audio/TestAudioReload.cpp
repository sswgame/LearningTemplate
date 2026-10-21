#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioReloadCache.h"
#include "Engine/Audio/IAudioService.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioReloadTest — 믹서 그래프 · 이벤트 라이브러리 파일을 다시 읽어 바꾸는 핫 리로드(서비스와 에셋 캐시 어댑터)
// ------------------------------------------------------------------------------

namespace
{
    struct AudioReloadTestInternal
    {
        static sw::string makeLibraryXML( const utf8* pEventName, const utf8* pBus )
        {
            return sw::string( "<AudioEventLibrary><_listEvent><AudioEventDesc _name=\"" ) + pEventName + "\" _bus=\"" + pBus +
                   "\"><_listClip><AudioClipEntry _path=\"test/none\" /></_listClip></AudioEventDesc></_listEvent></AudioEventLibrary>";
        }

        static sw::string makeTwoEventLibraryXML( const utf8* pFirstEvent, const utf8* pSecondEvent )
        {
            return sw::string( "<AudioEventLibrary><_listEvent>" ) +
                   "<AudioEventDesc _name=\"" + pFirstEvent + "\" _bus=\"sfx\"><_listClip><AudioClipEntry _path=\"test/none\" /></_listClip></AudioEventDesc>" +
                   "<AudioEventDesc _name=\"" + pSecondEvent + "\" _bus=\"sfx\"><_listClip><AudioClipEntry _path=\"test/none\" /></_listClip></AudioEventDesc>" +
                   "</_listEvent></AudioEventLibrary>";
        }

        /** @brief master 아래 sfx · music(와 @p pExtraBus 가 있으면 그것)의 그래프입니다. */
        static sw::string makeMixerXML( const utf8* pExtraBus )
        {
            sw::string text = "<AudioMixerDesc><_listBus><AudioBusDesc _name=\"master\" /><AudioBusDesc _name=\"sfx\" _parent=\"master\" /><AudioBusDesc _name=\"music\" _parent=\"master\" />";
            if ( pExtraBus != nullptr )
                text += sw::string( "<AudioBusDesc _name=\"" ) + pExtraBus + "\" _parent=\"master\" />";
            return text + "</_listBus></AudioMixerDesc>";
        }

        static void write( const sw::string& path, const sw::string& text )
        {
            SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, text ) );
        }
    };
} // namespace

/**
 * @brief [AudioReloadTest] 이벤트 라이브러리를 고치면 같은 이름으로 바뀌고, 깨진 파일은 지금 것을 그대로 둔다
 */
SW_TEST_CASE( AudioReloadTest, EventLibraryReloadsFromTheChangedFile )
{
    test::ScopedLogCollector logs; // 없는 클립 · 깨진 파일의 오류와 경고가 로그에 남는다
    const sw::string         folder   = test::makeTempDirectory( "audio_reload_library" );
    const sw::string         pathDisk = sw::FileUtil::joinPath( folder, "game.audioevents.xml" );
    const sw::string         path     = test::makeTempResourceID( pathDisk );
    AudioReloadTestInternal::write( pathDisk, AudioReloadTestInternal::makeLibraryXML( "Alpha", "sfx" ) );

    sw::unique_ptr<sw::IAudioService> pService = sw::IAudioService::createNull();
    SW_ASSERT_TRUE( pService->initialize() );
    sw::AudioEventLibrary library;
    SW_ASSERT_TRUE( library.loadFromResource( path ) );
    SW_ASSERT_TRUE( pService->getEngine().loadEventLibrary( sw::hashed_string( path ), library ) );
    SW_EXPECT_TRUE( pService->getEngine().hasEvent( sw::hashed_string( "Alpha" ) ) );
    SW_EXPECT_TRUE( pService->isDataFile( path ) );
    SW_EXPECT_FALSE( pService->isDataFile( sw::FileUtil::joinPath( folder, "other.audioevents.xml" ) ) );

    // 이벤트를 하나 더한 파일 → 같은 이름이라 두 라이브러리가 되지 않고 교체된다.
    AudioReloadTestInternal::write( pathDisk, AudioReloadTestInternal::makeTwoEventLibraryXML( "Alpha", "Beta" ) );
    SW_EXPECT_TRUE( pService->reloadDataFile( path ) );
    SW_EXPECT_TRUE( pService->getEngine().hasEvent( sw::hashed_string( "Beta" ) ) );
    SW_EXPECT_TRUE( pService->getEngine().hasEvent( sw::hashed_string( "Alpha" ) ) );

    // 깨진 파일은 거절하고 지금 것을 쓴다.
    AudioReloadTestInternal::write( pathDisk, "<AudioEventLibrary><_listEvent><AudioEventDesc" );
    SW_EXPECT_FALSE( pService->reloadDataFile( path ) );
    SW_EXPECT_TRUE( pService->getEngine().hasEvent( sw::hashed_string( "Beta" ) ) );

    // 오디오 데이터가 아닌 경로는 건드리지 않는다.
    SW_EXPECT_FALSE( pService->reloadDataFile( sw::FileUtil::joinPath( folder, "nothing.audioevents.xml" ) ) );
    pService->shutdown();
}

/**
 * @brief [AudioReloadTest] 믹서 그래프를 고치면 새 버스가 생기고(그 버스로 보내는 라이브러리가 로드되고), 깨진 파일은 지금 그래프를 둔다
 */
SW_TEST_CASE( AudioReloadTest, MixerGraphReloadsFromTheChangedFile )
{
    test::ScopedLogCollector logs;
    const sw::string         folder          = test::makeTempDirectory( "audio_reload_mixer" );
    const sw::string         mixerPathDisk   = sw::FileUtil::joinPath( folder, "game.audiomixer.xml" );
    const sw::string         mixerPath       = test::makeTempResourceID( mixerPathDisk );
    const sw::string         libraryPathDisk = sw::FileUtil::joinPath( folder, "extra.audioevents.xml" );
    const sw::string         libraryPath     = test::makeTempResourceID( libraryPathDisk );
    AudioReloadTestInternal::write( mixerPathDisk, AudioReloadTestInternal::makeMixerXML( nullptr ) );
    AudioReloadTestInternal::write( libraryPathDisk, AudioReloadTestInternal::makeLibraryXML( "Gamma", "extra" ) );

    sw::unique_ptr<sw::IAudioService> pService = sw::IAudioService::createNull();
    SW_ASSERT_TRUE( pService->initialize() );
    SW_ASSERT_TRUE( pService->loadMixer( mixerPath ) );
    SW_EXPECT_STREQ( mixerPath.c_str(), pService->getMixerPath().c_str() );
    SW_EXPECT_TRUE( pService->isDataFile( mixerPath ) );

    sw::AudioEventLibrary library;
    SW_ASSERT_TRUE( library.loadFromResource( libraryPath ) );
    SW_EXPECT_FALSE( pService->getEngine().loadEventLibrary( sw::hashed_string( libraryPath ), library ) ); // 'extra' 버스가 아직 없다

    AudioReloadTestInternal::write( mixerPathDisk, AudioReloadTestInternal::makeMixerXML( "extra" ) );
    SW_EXPECT_TRUE( pService->reloadDataFile( mixerPath ) );
    SW_EXPECT_TRUE( pService->getEngine().loadEventLibrary( sw::hashed_string( libraryPath ), library ) );

    AudioReloadTestInternal::write( mixerPathDisk, "<AudioMixerDesc><_listBus><AudioBusDesc _name=\"sfx\" _parent=\"nowhere\" /></_listBus></AudioMixerDesc>" );
    SW_EXPECT_FALSE( pService->reloadDataFile( mixerPath ) );
    SW_EXPECT_STREQ( mixerPath.c_str(), pService->getMixerPath().c_str() );
    pService->shutdown();
}

/**
 * @brief [AudioReloadTest] 에셋 캐시 어댑터의 종류 이름은 "AudioData"이고, 엔진 서비스가 묶이지 않은 곳에서는 아무것도 캐시하지 않는다
 */
SW_TEST_CASE( AudioReloadTest, CacheAdapterNamesItsKind )
{
    sw::AudioReloadCache cache;
    SW_EXPECT_STREQ( "AudioData", cache.getAssetKindName() );
    cache.clear(); // 아무것도 비우지 않는다 — 호출해도 안전하다
}
