#include "pch.h"

#include "Engine/Graphics/Texture/Texture2D.h"

#include "TestFramework/TestFramework.h"

// Texture2DMipDropTest — 텍스처 품질(gv_textureMipDrop)이 읽을 때 버리는 위쪽 밉의 수와 건너뛸 바이트. 디바이스 없음(nogpu).

/**
 * @brief [Texture2DMipDropTest] 요청한 수만큼 위쪽 밉을 버리고 건너뛸 바이트는 버린 밉 크기의 합이다
 * @details 1024² RGBA8(4 바이트) 11 밉에서 둘을 버리면 1024² × 4 + 512² × 4 바이트를 건너뛰고 256² 이 밉 0 이 된다.
 */
SW_TEST_CASE( Texture2DMipDropTest, DropsRequestedTopMips )
{
    const sw::Texture2DMipDrop drop = sw::Texture2D::computeMipDrop( sw::RHIFormat::R8G8B8A8_UNORM, 1024, 1024, 11, 2 );
    SW_EXPECT_EQUAL( uint32( 2 ), drop._mipCount );
    SW_EXPECT_EQUAL( uint64( 1024 * 1024 * 4 + 512 * 512 * 4 ), drop._skippedBytes );

    const sw::Texture2DMipDrop none = sw::Texture2D::computeMipDrop( sw::RHIFormat::R8G8B8A8_UNORM, 1024, 1024, 11, 0 );
    SW_EXPECT_EQUAL( uint32( 0 ), none._mipCount );
    SW_EXPECT_EQUAL( uint64( 0 ), none._skippedBytes );
}

/**
 * @brief [Texture2DMipDropTest] 밉이 하나뿐이거나 작은 텍스처, 블록 배수가 아닌 크기는 줄이지 않는다
 * @details 밉 하나(UI · 룩업)는 버릴 밉이 없다. 128² 는 한 번 버리면 64²(하한)라 하나만 버린다. BC1 의 296² → 148² 은 4 의 배수지만 74² 은 아니라서 하나만 버린다.
 */
SW_TEST_CASE( Texture2DMipDropTest, KeepsSmallSingleMipAndUnalignedTextures )
{
    SW_EXPECT_EQUAL( uint32( 0 ), sw::Texture2D::computeMipDrop( sw::RHIFormat::R8G8B8A8_UNORM, 1024, 1024, 1, 2 )._mipCount );
    SW_EXPECT_EQUAL( uint32( 1 ), sw::Texture2D::computeMipDrop( sw::RHIFormat::R8G8B8A8_UNORM, 128, 128, 8, 2 )._mipCount );
    SW_EXPECT_EQUAL( uint32( 1 ), sw::Texture2D::computeMipDrop( sw::RHIFormat::BC1_UNORM, 296, 296, 9, 3 )._mipCount );
}
