// 현지화 핫 리로드의 길 — 에셋 캐시 레지스트리의 "StringTable" 어댑터가 바뀐 번역 표를 엔진 관리자에 넘겨 다시 읽힌다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/Tests/LocalizationTestUtil.h"

namespace
{
    /** @brief 엔진 관리자의 게임 범위 프로젝트 · 언어를 소멸자에서 되돌린다(도메인은 그 뒤에 뗀다). */
    struct LocalizationReloadCacheTest
    {

        sw::test::ScopedScratchDomain _domain;
        sw::LocalizationManager&      _loc;
        sw::string                    _previousLanguage;
        LocalizationReloadCacheTest()
            : _loc{ sw::engine::getSubsystem<sw::LocalizationManager>() }
            , _previousLanguage{ _loc.getCurrentLanguage() }
        {
        }
        ~LocalizationReloadCacheTest()
        {
            _loc.unmountProjects( sw::LocalizationScope::Game );
            _loc.clearMissingKeys();
            (void)_loc.setCurrentLanguage( _previousLanguage ); // 앞 언어는 이미 쓰던 것이다
        }
        LocalizationReloadCacheTest( const LocalizationReloadCacheTest& )            = delete;
        LocalizationReloadCacheTest& operator=( const LocalizationReloadCacheTest& ) = delete;
    };

    sw::string lookup( const sw::LocalizationManager& loc, const utf8* pKey )
    {
        const utf8* pText = loc.getString( sw::hashed_string( pKey ) );
        return pText != nullptr ? sw::string( pText ) : sw::string( "<null>" );
    }
} // namespace

/**
 * @brief [LocalizationReloadCacheTest] 바뀐 파일의 종류로 캐시를 찾아 리로드 함수를 부르면(`AssetHotReload` 의 길) 번역이 바뀌고, 캐시 비우기는 글을 지우지 않는다
 * @details 로컬라이제이션만 그 레지스트리 밖에 있으면 번역가가 저장해도 실행 중인 게임의 글이 그대로다.
 */
SW_TEST_CASE_F( LocalizationReloadCacheTest, HotReloadGoesThroughTheAssetCacheRegistry )
{
    using sw::test::LocalizationTestUtil;
    const sw::string folder = _domain.getFolder();
    LocalizationTestUtil::writeSourceTable( folder, "en", R"("ui.title": { "source": "Mystery Island" })" );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "신비의 섬" })" );
    const sw::string projectPath = LocalizationTestUtil::writeProject( folder, "en", R"([ "ko" ])" );
    SW_REQUIRE_TRUE( _loc.mountProject( sw::test::ScopedScratchDomain::toID( projectPath ), sw::LocalizationScope::Game ) );
    SW_REQUIRE_TRUE( _loc.setCurrentLanguage( "ko" ) );
    SW_EXPECT_EQUAL( sw::string( "신비의 섬" ), lookup( _loc, "ui.title" ) );

    sw::AssetManager&   assets = sw::engine::getSubsystem<sw::AssetManager>();
    sw::AssetCacheBase* pCache = assets.findAssetCache( "StringTable" );
    SW_REQUIRE_NOT_NULL( pCache );
    const sw::AssetCacheReloadFunction pfnReload = assets.findReloadFunction( pCache );
    SW_REQUIRE_TRUE( pfnReload != nullptr );

    const sw::AssetID translationID = sw::test::ScopedScratchDomain::toID( sw::FileUtil::joinPath( folder, "ko.translation.xml" ) );
    SW_EXPECT_TRUE( pCache->isCached( translationID.view() ) );
    LocalizationTestUtil::writeTranslation( folder, "ko", R"("ui.title": { "text": "수수께끼 섬" })" );
    pfnReload( *pCache, translationID );
    SW_EXPECT_EQUAL( sw::string( "수수께끼 섬" ), lookup( _loc, "ui.title" ) );

    pCache->clear(); // 캐시 비우기(재초기화)는 화면의 글을 지우지 않는다
    SW_EXPECT_EQUAL( sw::string( "수수께끼 섬" ), lookup( _loc, "ui.title" ) );
}
