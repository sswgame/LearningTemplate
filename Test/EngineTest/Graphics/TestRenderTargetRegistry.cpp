#include "pch.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [RenderTargetRegistryTest] 공개한 핸들만 `isPublished` 이고, 목록을 비우거나 갈아 끼우면 옛 핸들은 아니다
 * @details 창 크기가 바뀌면 트랜지언트를 놓기 전에 목록을 먼저 비운다. 렌더 스레드는 이것으로 UI 가 요청한 죽은 핸들을 읽기 전에 걸러,
 *          Vulkan 에서 `readbackTexture2D: texture … or the device is not usable` 오류가 나지 않게 한다.
 */
SW_TEST_CASE( RenderTargetRegistryTest, OnlyPublishedHandlesAreReadable )
{
    RenderTargetRegistry registry;
    SW_EXPECT_FALSE( registry.isPublished( 7 ) );

    vector<RenderTargetInfo> listTarget;
    RenderTargetInfo&        color = listTarget.emplace_back();
    color._name                    = "SceneColor";
    color._texture                 = 7;
    RenderTargetInfo& depth        = listTarget.emplace_back();
    depth._name                    = "SceneDepth";
    depth._texture                 = 9;
    registry.publish( listTarget );
    SW_EXPECT_TRUE( registry.isPublished( 7 ) );
    SW_EXPECT_TRUE( registry.isPublished( 9 ) );
    SW_EXPECT_FALSE( registry.isPublished( 8 ) );

    // 크기가 바뀐 뒤 새 핸들로 다시 공개하면 옛 핸들은 죽은 것이다.
    listTarget[0]._texture = 11;
    registry.publish( listTarget );
    SW_EXPECT_FALSE( registry.isPublished( 7 ) );
    SW_EXPECT_TRUE( registry.isPublished( 11 ) );

    registry.clear();
    SW_EXPECT_FALSE( registry.isPublished( 11 ) );
    SW_EXPECT_FALSE( registry.isPublished( 9 ) );
}
