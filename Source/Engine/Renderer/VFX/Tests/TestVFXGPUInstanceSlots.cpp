#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

#include "vfx_simulate.hlsli"

#include <cstddef>

// GPU 메시 입자의 인스턴스 칸(dead list) 계약 — 인자 버퍼의 구간이 겹치지 않고, 빈 칸 플래그를 메시 쓰기가 켜고 컬링이 빼며, C++ 인스턴스 원소의 플래그 칸이 셰이더와 같은 자리다.

using namespace sw;

namespace
{
    struct VFXGPUInstanceSlotsTestUtil
    {
        static string readEngineShader( const utf8* pFileName )
        {
            const string projectRoot = FileUtil::normalizeSeparators( ProcessRoots::get()._project.str() );
            string       text;
            if ( projectRoot.empty() == false )
                std::ignore = FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( FileUtil::joinPath( projectRoot, "Content/engine/shaders" ), pFileName ) ), text );
            return text;
        }

        static bool contains( const string& text, const utf8* pNeedle ) { return text.find( pNeedle ) != string::npos; }
    };
    using Util = VFXGPUInstanceSlotsTestUtil;
} // namespace

/**
 * @brief [VFXGPUInstanceSlotsTest] 인자 버퍼의 고정 단어 · 빈 칸 스택 · 살아 있는 칸 목록 · 다중 그룹 칸이 풀 크기마다 겹치지 않고 차례로 놓인다
 * @details 빈 칸 스택과 살아 있는 칸 목록은 풀 크기씩이다(살아 있는 수 + 스택 깊이 = 풀). 겹치면 메시 쓰기가 다른 구간의 단어를 칸 번호로 읽는다.
 */
SW_TEST_CASE( VFXGPUInstanceSlotsTest, ArgsRegionsDoNotOverlap )
{
    SW_EXPECT_TRUE( SW_VFX_ARGS_WORD_ALIVE < SW_VFX_ARGS_WORD_COUNT && SW_VFX_ARGS_WORD_HALF < SW_VFX_ARGS_WORD_COUNT && SW_VFX_ARGS_WORD_DROPPED < SW_VFX_ARGS_WORD_COUNT &&
                    SW_VFX_ARGS_WORD_SCRATCH_TOTAL < SW_VFX_ARGS_WORD_COUNT );
    SW_EXPECT_TRUE( SW_VFX_ARGS_WORD_DEAD_FIRST >= SW_VFX_ARGS_WORD_COUNT );
    const uint32 arrPoolSize[]{ 1u, 64u, SW_VFX_SIMULATE_THREAD_COUNT + 1u, SW_VFX_MULTI_GROUP_MIN_POOL, SW_VFX_MULTI_GROUP_MIN_POOL * 3u + 7u };
    for ( const uint32 poolSize : arrPoolSize )
    {
        SW_EXPECT_EQUAL( SW_VFX_ARGS_WORD_DEAD_FIRST + poolSize, SW_VFX_ARGS_WORD_ALIVE_SLOT_FIRST( poolSize ) );
        SW_EXPECT_EQUAL( SW_VFX_ARGS_WORD_ALIVE_SLOT_FIRST( poolSize ) + poolSize, SW_VFX_ARGS_WORD_GROUP_FIRST( poolSize ) );
    }
}

/**
 * @brief [VFXGPUInstanceSlotsTest] 빈 칸 플래그는 인스턴스 원소의 마지막 단어이고, 메시 쓰기가 빈 칸에 켜고 살아 있는 칸에 끄며, 컬링이 켜진 칸을 세지 않는다
 * @details 플래그를 켜도 컬링이 빼지 않으면 죽은 칸이 크기 0 으로 그려져 간접 인자의 수가 풀 크기가 된다. C++ 는 늘 0 을 올린다(GPU 가 쓰는 칸만 켠다).
 */
SW_TEST_CASE( VFXGPUInstanceSlotsTest, EmptyFlagIsWrittenByMeshPassAndCulled )
{
    SW_EXPECT_EQUAL( sizeof( GPUInstance ) - sizeof( uint32 ), offsetof( GPUInstance, _instanceFlags ) );
    SW_EXPECT_EQUAL( 0u, GPUInstance{}._instanceFlags );

    const string instanceText = Util::readEngineShader( "instance_data.hlsli" );
    const string meshText     = Util::readEngineShader( "vfx_mesh_instance.hlsl" );
    const string cullText     = Util::readEngineShader( "gpu_cull.hlsl" );
    const string simulateText = Util::readEngineShader( "vfx_simulate.hlsl" );
    SW_REQUIRE_FALSE( instanceText.empty() || meshText.empty() || cullText.empty() || simulateText.empty() );
    SW_EXPECT_TRUE( Util::contains( instanceText, "uint     _instanceFlags;" ) );
    SW_EXPECT_TRUE( Util::contains( instanceText, "static const uint kInstanceFlagEmpty" ) );
    SW_EXPECT_TRUE( Util::contains( meshText, "instance._instanceFlags |= kInstanceFlagEmpty;" ) );
    SW_EXPECT_TRUE( Util::contains( meshText, "instance._instanceFlags &= ~kInstanceFlagEmpty;" ) );
    SW_EXPECT_TRUE( Util::contains( cullText, "( instance._instanceFlags & kInstanceFlagEmpty ) != 0u" ) );
    // 시뮬레이션은 재생 시작에 스택을 채우고, 생성이 꺼내고, 두 경로(그룹 하나 · 다중 그룹)가 죽은 칸을 돌려주며, 렌더 입력 순서로 살아 있는 칸을 적는다.
    SW_EXPECT_TRUE( Util::contains( simulateText, "resetDeadSlots(" ) );
    SW_EXPECT_TRUE( Util::contains( simulateText, "popDeadSlot(" ) );
    SW_EXPECT_TRUE( Util::contains( simulateText, "storeArgsWord( SW_VFX_ARGS_WORD_ALIVE_SLOT_FIRST( poolSize ) + outputIndex" ) );
}
