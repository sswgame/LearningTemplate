#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderReachableText.h"

using namespace sw;

// 진입점이 닿는 글(ShaderReachableText) — 셰이더 DDC 의 바이트코드 키가 이것을 해시한다. 손으로 쓴 전처리 글로 본다. 컴파일러 없음.

namespace
{
    struct ShaderReachableTextTestUtil
    {
        static bool contains( string_view text, string_view part ) { return text.find( part ) != string_view::npos; }
    };
} // namespace

/**
 * @brief [ShaderReachableTextTest] `#line` 줄 · 빈칸의 크기 · 빈 줄은 글에 들지 않고, 낱말이 하나라도 다르면 다른 글이다
 * @details 변이: 줄바꿈까지 지우면 `#pragma` 줄과 다음 줄이 이어지고, 빈칸을 아예 지우면 `return x` 와 `returnx`, `a+ +b` 와 `a++b` 가 같아진다.
 */
SW_TEST_CASE( ShaderReachableTextTest, LineNumbersAndSpacingAreIgnored )
{
    const string base = ShaderReachableText::make( "#line 1 \"a.hlsl\"\nfloat4 Main()\n{\n    return x;\n}\n", "Main" );
    SW_EXPECT_EQUAL( base, ShaderReachableText::make( "\r\n  #line 77 \"b.hlsli\"\r\n\r\nfloat4\tMain()\r\n{\r\n\r\n  return  x;\r\n}", "Main" ) );
    SW_EXPECT_TRUE( base != ShaderReachableText::make( "float4 Main()\n{\n    return y;\n}\n", "Main" ) );
    SW_EXPECT_TRUE( base != ShaderReachableText::make( "float4 Main()\n{\n    returnx;\n}\n", "Main" ) );
    SW_EXPECT_TRUE( ShaderReachableText::make( "float Main() { return a+ +b; }", "Main" ) != ShaderReachableText::make( "float Main() { return a++b; }", "Main" ) );
    SW_EXPECT_TRUE( ShaderReachableText::make( "#pragma a\nfloat Main() { return 1; }", "Main" ) != ShaderReachableText::make( "#pragma a float\nMain() { return 1; }", "Main" ) );
}

/**
 * @brief [ShaderReachableTextTest] 정점 진입점의 글은 픽셀 쪽 함수 · 자원 · 상수가 바뀌어도 같고, 정점이 부르는 함수가 바뀌면 다르다
 * @details 변이: 전역을 모두 남기면 픽셀 define 하나가 정점 셰이더의 키를 가른다(이 형식이 있는 까닭).
 */
SW_TEST_CASE( ShaderReachableTextTest, UnreachableDeclarationsDoNotChangeTheText )
{
    const string_view shared = "Texture2D<float4> g_Albedo : register( t0 );\nSamplerState g_Sampler : register( s0 );\ncbuffer PassCB : register( b0 ) { float4x4 g_View; float g_Time; };\n"
                               "struct Vertex { float4 _position : SV_POSITION; float2 _uv : TEXCOORD0; };\nfloat4 transform( float3 p ) { return mul( float4( p, 1.0f ), g_View ); }\n"
                               "Vertex VSMain( float3 p : POSITION ) { Vertex v; v._position = transform( p ); v._uv = p.xy; return v; }\n";
    string            first{ shared };
    first.append( "float3 shade( float2 uv ) { return g_Albedo.Sample( g_Sampler, uv ).rgb; }\nfloat4 PSMain( Vertex v ) : SV_Target { return float4( shade( v._uv ), 1 ); }\n" );
    string second{ shared };
    second.append( "static const float kFade = 0.5f;\nfloat4 PSMain( Vertex v ) : SV_Target { return kFade * g_Time; }\n" );

    const string vertexText = ShaderReachableText::make( first, "VSMain" );
    SW_EXPECT_EQUAL( vertexText, ShaderReachableText::make( second, "VSMain" ) );
    SW_EXPECT_TRUE( ShaderReachableText::make( first, "PSMain" ) != ShaderReachableText::make( second, "PSMain" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( vertexText, "PassCB" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( vertexText, "transform" ) );
    SW_EXPECT_FALSE( ShaderReachableTextTestUtil::contains( vertexText, "g_Albedo" ) );
    SW_EXPECT_FALSE( ShaderReachableTextTestUtil::contains( vertexText, "PSMain" ) );

    string       third{ shared };
    const size_t at = third.find( "1.0f" );
    third.replace( at, 4, "0.0f" );
    SW_EXPECT_TRUE( vertexText != ShaderReachableText::make( third, "VSMain" ) );
}

/**
 * @brief [ShaderReachableTextTest] 겹친 이름 · 이름공간 안 · 구조체 함수 · 구조체 밖 정의 · 문자열로 가리킨 함수는 이름이 쓰이면 남고, 쓰지 않는 구조체 함수는 빠진다
 * @details 변이: 문자열 안의 이름을 세지 않으면 헐 셰이더의 패치 상수 함수가 빠진다.
 */
SW_TEST_CASE( ShaderReachableTextTest, NamesReachOverloadsMembersAndStrings )
{
    const string_view text      = "namespace Math { float square( float x ) { return x * x; } float2 square( float2 x ) { return x * x; } float cube( float x ) { return x; } }\n"
                                  "struct Surface\n{\nfloat3 _normal;\nfloat dotUp() { return _normal.y; }\nfloat unusedMember() { return 9; }\nstatic float outside( float x );\n};\n"
                                  "float Surface::outside( float x ) { return Math::square( x ); }\nstruct Never { float _value; };\n"
                                  "float PatchConstants() { return 3; }\n[patchconstantfunc( \"PatchConstants\" )] float HSMain( Surface s ) { return s.dotUp() + Surface::outside( 2 ); }\n";
    const string      reachable = ShaderReachableText::make( text, "HSMain" );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( reachable, "float2 square" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( reachable, "dotUp" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( reachable, "float PatchConstants" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( reachable, "Surface::outside( float x ) {" ) );
    SW_EXPECT_FALSE( ShaderReachableTextTestUtil::contains( reachable, "cube" ) );
    SW_EXPECT_FALSE( ShaderReachableTextTestUtil::contains( reachable, "unusedMember" ) );
    SW_EXPECT_FALSE( ShaderReachableTextTestUtil::contains( reachable, "Never" ) );
}

/**
 * @brief [ShaderReachableTextTest] 괄호가 맞지 않거나 끝이 없는 글은 걷어 내지 않고 모두 남긴다
 */
SW_TEST_CASE( ShaderReachableTextTest, UnbalancedTextIsKeptWhole )
{
    const string reachable = ShaderReachableText::make( "float unused() { return 1; }\nfloat Main() { return 2;\n", "Main" );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( reachable, "unused" ) );
    SW_EXPECT_TRUE( ShaderReachableTextTestUtil::contains( ShaderReachableText::make( "namespace X\nfloat unused()", "Main" ), "unused" ) );
    SW_EXPECT_TRUE( ShaderReachableText::make( "", "Main" ).empty() );
}
