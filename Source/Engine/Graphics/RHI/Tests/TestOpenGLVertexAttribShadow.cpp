/**
 * @file TestOpenGLVertexAttribShadow.cpp
 * @brief GL 메시 VAO 속성 사본(`OpenGLVertexAttribShadow`) — 같은 값은 다시 걸지 않고, 지운 버퍼 이름 · VAO 재생성 뒤에는 다시 거는지.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/GL/OpenGLVertexAttribShadow.h"

using namespace sw;

namespace
{
    OpenGLVertexAttribShadow::Entry makeEntry( uint32 buffer, uint32 offset )
    {
        return OpenGLVertexAttribShadow::Entry{ buffer, 12, offset, RHIVertexAttributeFormat::Float3, SW_FALSE, SW_TRUE };
    }
} // namespace

SW_TEST_CASE( OpenGLVertexAttribShadowTest, SkipsUnchangedAndRebindsChanged )
{
    OpenGLVertexAttribShadow shadow;
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 7, 0 ) ) );  // 처음은 늘 건다
    SW_EXPECT_FALSE( shadow.update( 0, makeEntry( 7, 0 ) ) ); // 같은 값은 건너뛴다
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 7, 64 ) ) ); // 시작 바이트가 다르다
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 8, 64 ) ) ); // 버퍼가 다르다
    SW_EXPECT_TRUE( shadow.update( 0, OpenGLVertexAttribShadow::Entry::makeDisabled() ) );
    SW_EXPECT_FALSE( shadow.update( 0, OpenGLVertexAttribShadow::Entry::makeDisabled() ) );
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 8, 64 ) ) ); // 다시 켜면 포인터도 다시 건다
}

SW_TEST_CASE( OpenGLVertexAttribShadowTest, InvalidateBufferRebindsReusedName )
{
    // 지운 버퍼의 이름이 새 버퍼에 다시 나오면 값은 같아 보여도 다시 걸어야 한다(묶이지 않은 VAO 는 지운 버퍼를 붙든다).
    OpenGLVertexAttribShadow shadow;
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 7, 0 ) ) );
    SW_EXPECT_TRUE( shadow.update( 1, makeEntry( 9, 0 ) ) );
    shadow.invalidateBuffer( 7 );
    SW_EXPECT_TRUE( shadow.update( 0, makeEntry( 7, 0 ) ) );
    SW_EXPECT_FALSE( shadow.update( 1, makeEntry( 9, 0 ) ) ); // 다른 버퍼를 건 칸은 그대로
}

SW_TEST_CASE( OpenGLVertexAttribShadowTest, ResetRebindsEverything )
{
    OpenGLVertexAttribShadow shadow;
    SW_EXPECT_TRUE( shadow.update( 0, OpenGLVertexAttribShadow::Entry::makeDisabled() ) );
    SW_EXPECT_TRUE( shadow.update( 1, makeEntry( 9, 0 ) ) );
    shadow.reset();
    SW_EXPECT_TRUE( shadow.update( 0, OpenGLVertexAttribShadow::Entry::makeDisabled() ) );
    SW_EXPECT_TRUE( shadow.update( 1, makeEntry( 9, 0 ) ) );
    SW_EXPECT_TRUE( shadow.update( constant::render::kVertexAttributeCount, makeEntry( 9, 0 ) ) ); // 범위 밖은 늘 건다
}
