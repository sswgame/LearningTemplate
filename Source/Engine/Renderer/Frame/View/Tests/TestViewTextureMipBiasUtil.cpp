#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/View/ViewTextureMipBiasUtil.h"

// 뷰의 머티리얼 텍스처 밉 바이어스(R-15) — 배율 1 이면 0(골든 그대로), 절반으로 그려 키우면 -1, 크기를 모르면 0.

using namespace sw;

/** @brief [ViewTextureMipBiasTest] log2(렌더 높이 / 출력 높이) 이고 같은 크기 · 모르는 크기는 0 이다. 변이: 렌더 · 출력을 뒤바꾸면 부호가 바뀌어 진다. */
SW_TEST_CASE( ViewTextureMipBiasTest, Log2OfRenderOverOutput )
{
    SW_EXPECT_NEAR( 0.0f, ViewTextureMipBiasUtil::compute( 1080, 1080 ), 0.0f );
    SW_EXPECT_NEAR( -1.0f, ViewTextureMipBiasUtil::compute( 540, 1080 ), 1.0e-6f );
    SW_EXPECT_NEAR( -0.5849625f, ViewTextureMipBiasUtil::compute( 720, 1080 ), 1.0e-5f ); // 품질 모드 배율 2/3
    SW_EXPECT_NEAR( 1.0f, ViewTextureMipBiasUtil::compute( 2160, 1080 ), 1.0e-6f );       // 크게 그려 줄이면 양수
    SW_EXPECT_NEAR( 0.0f, ViewTextureMipBiasUtil::compute( 0, 1080 ), 0.0f );
    SW_EXPECT_NEAR( 0.0f, ViewTextureMipBiasUtil::compute( 1080, 0 ), 0.0f );
}
