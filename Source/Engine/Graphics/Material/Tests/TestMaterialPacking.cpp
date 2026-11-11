#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"

// MaterialPacking — 타입 표 · 정렬 · 리플렉션 타입 대응 · 불리언 글 · 폴백 원소 · 값을 바이트로 묶기. 리플렉션 enum(`_enumType` 이 등록된 이름인 경우)은 TypeRegistry 서브시스템이 있어야 해서 여기서 보지 않는다.

using namespace sw;

namespace
{
    struct MaterialPackingTestUtil
    {
        static MaterialProperty makeProperty( MaterialPropertyType type, string_view value )
        {
            MaterialProperty prop{};
            prop._name  = "_test";
            prop._type  = type;
            prop._value = value;
            return prop;
        }

        static void addEnumEntry( MaterialProperty& prop, const utf8* pName, uint32 value )
        {
            MaterialEnumEntry entry{};
            entry._name  = pName;
            entry._value = value;
            prop._listEnumEntry.push_back( entry );
        }

        static ShaderVariableInfo makeMember( const utf8* pName, const utf8* pType, uint32 offset, uint32 size )
        {
            ShaderVariableInfo member{};
            member._name   = pName;
            member._type   = pType;
            member._offset = offset;
            member._size   = size;
            return member;
        }

        static float32 readFloat( const vector<uint8>& buffer, uint32 offset )
        {
            float32 value{ 0.0f };
            Memory::copy( &value, buffer.data() + offset, sizeof( value ) );
            return value;
        }

        static uint32 readUint( const vector<uint8>& buffer, uint32 offset )
        {
            uint32 value{ 0 };
            Memory::copy( &value, buffer.data() + offset, sizeof( value ) );
            return value;
        }

        static int32 readInt( const vector<uint8>& buffer, uint32 offset )
        {
            int32 value{ 0 };
            Memory::copy( &value, buffer.data() + offset, sizeof( value ) );
            return value;
        }

        static constexpr uint8  kUntouchedByte = 7;
        static constexpr uint32 kUntouchedWord = 0x07070707u;
    };
} // namespace

/**
 * @brief [MaterialPackingTest] 타입 이름은 표의 크기와 함께 읽히고, 별칭은 같은 타입이며, 모르는 이름 · 대소문자가 다른 이름은 Unknown 과 크기 0 이다
 */
SW_TEST_CASE( MaterialPackingTest, StringToTypeReadsNamesAndAliases )
{
    uint32 size{ 99 };
    SW_EXPECT_EQUAL( MaterialPropertyType::Float4x4, MaterialPacking::stringToType( "Float4x4", size ) );
    SW_EXPECT_EQUAL( 64u, size );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float4, MaterialPacking::stringToType( "Vector", size ) );
    SW_EXPECT_EQUAL( 16u, size );
    SW_EXPECT_EQUAL( MaterialPropertyType::Bool, MaterialPacking::stringToType( "Toggle", size ) );
    SW_EXPECT_EQUAL( 4u, size );
    SW_EXPECT_EQUAL( MaterialPropertyType::Keyword, MaterialPacking::stringToType( "StaticSwitch", size ) );
    SW_EXPECT_EQUAL( 0u, size );

    size = 99;
    SW_EXPECT_EQUAL( MaterialPropertyType::Unknown, MaterialPacking::stringToType( "float4", size ) );
    SW_EXPECT_EQUAL( 0u, size );
    size = 99;
    SW_EXPECT_EQUAL( MaterialPropertyType::Unknown, MaterialPacking::stringToType( "", size ) );
    SW_EXPECT_EQUAL( 0u, size );
}

/**
 * @brief [MaterialPackingTest] 모든 타입은 표 이름으로 돌아왔다 다시 같은 타입으로 읽힌다(별칭이 표준 이름을 가리지 않는다)
 */
SW_TEST_CASE( MaterialPackingTest, TypeNamesRoundTrip )
{
    const uint8 typeCount = static_cast<uint8>( MaterialPropertyType::Unknown );
    for ( uint8 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
    {
        const MaterialPropertyType type = static_cast<MaterialPropertyType>( typeIndex );
        uint32                     size{ 0 };
        SW_EXPECT_EQUAL( type, MaterialPacking::stringToType( MaterialPacking::typeToString( type ), size ) );
        SW_EXPECT_EQUAL( MaterialPacking::computePackedSizeOf( type ), size );
    }
    SW_EXPECT_TRUE( string_view( MaterialPacking::typeToString( MaterialPropertyType::Unknown ) ) == "Unknown" );
    SW_EXPECT_EQUAL( 0u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Unknown ) );
}

/**
 * @brief [MaterialPackingTest] 패킹 크기는 타입 칸 수대로이고, 키워드는 버퍼에 들어가지 않으며, 텍스처 네 종류만 텍스처 타입이다
 */
SW_TEST_CASE( MaterialPackingTest, PackedSizeAndTypeClasses )
{
    SW_EXPECT_EQUAL( 4u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Float ) );
    SW_EXPECT_EQUAL( 8u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Uint2 ) );
    SW_EXPECT_EQUAL( 12u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Int3 ) );
    SW_EXPECT_EQUAL( 16u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Color ) );
    SW_EXPECT_EQUAL( 64u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Float4x4 ) );
    SW_EXPECT_EQUAL( 4u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Texture2DArray ) );
    SW_EXPECT_EQUAL( 0u, MaterialPacking::computePackedSizeOf( MaterialPropertyType::Keyword ) );

    SW_EXPECT_TRUE( MaterialPacking::isTextureType( MaterialPropertyType::Texture2D ) );
    SW_EXPECT_TRUE( MaterialPacking::isTextureType( MaterialPropertyType::TextureCube ) );
    SW_EXPECT_TRUE( MaterialPacking::isTextureType( MaterialPropertyType::Texture3D ) );
    SW_EXPECT_TRUE( MaterialPacking::isTextureType( MaterialPropertyType::Texture2DArray ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureType( MaterialPropertyType::Uint ) );

    SW_EXPECT_TRUE( MaterialPacking::isNonBufferType( MaterialPropertyType::Keyword ) );
    SW_EXPECT_FALSE( MaterialPacking::isNonBufferType( MaterialPropertyType::Bool ) );
}

/**
 * @brief [MaterialPackingTest] CPU 전용 타입의 기본 셰이더 타입: 불리언 · 열거 · 마스크 · 텍스처는 Uint, Range 는 Float, Color 는 Float4, 키워드는 없음, 나머지는 그대로
 */
SW_TEST_CASE( MaterialPackingTest, DefaultShaderTypes )
{
    SW_EXPECT_EQUAL( MaterialPropertyType::Uint, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::Bool ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Uint, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::BitFlag ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Uint, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::TextureCube ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::Range ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float4, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::Color ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Unknown, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::Keyword ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Int3, MaterialPacking::getDefaultShaderTypeFor( MaterialPropertyType::Int3 ) );
}

/**
 * @brief [MaterialPackingTest] 리플렉션 이름이 숫자 타입이면 그대로, Bool 은 Uint, CPU 전용 이름 · 빈 이름은 크기로 고르고, 맞는 크기가 없으면 Unknown 이다
 */
SW_TEST_CASE( MaterialPackingTest, ShaderTypeFromReflectionName )
{
    SW_EXPECT_EQUAL( MaterialPropertyType::Uint2, MaterialPacking::shaderTypeFromReflectionName( "Uint2", 0 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Int, MaterialPacking::shaderTypeFromReflectionName( "Int", 64 ) ); // 이름이 크기보다 먼저다
    SW_EXPECT_EQUAL( MaterialPropertyType::Uint, MaterialPacking::shaderTypeFromReflectionName( "Bool", 4 ) );

    SW_EXPECT_EQUAL( MaterialPropertyType::Float4, MaterialPacking::shaderTypeFromReflectionName( "Color", 16 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float, MaterialPacking::shaderTypeFromReflectionName( "Texture2D", 4 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float3, MaterialPacking::shaderTypeFromReflectionName( "Enum", 12 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float2, MaterialPacking::shaderTypeFromReflectionName( "", 8 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float4x4, MaterialPacking::shaderTypeFromReflectionName( "", 64 ) );

    SW_EXPECT_EQUAL( MaterialPropertyType::Unknown, MaterialPacking::shaderTypeFromReflectionName( "", 5 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Unknown, MaterialPacking::shaderTypeFromReflectionName( "SomeStruct", 20 ) );
}

/**
 * @brief [MaterialPackingTest] 정렬: 4 바이트 이하는 4, 벡터(8 · 12 · 16)와 행렬(64)은 16 바이트 경계이고, 이미 경계면 그대로다
 */
SW_TEST_CASE( MaterialPackingTest, AlignOffsetBoundaries )
{
    SW_EXPECT_EQUAL( 0u, MaterialPacking::alignOffset( 0, 4 ) );
    SW_EXPECT_EQUAL( 8u, MaterialPacking::alignOffset( 5, 4 ) );
    SW_EXPECT_EQUAL( 8u, MaterialPacking::alignOffset( 8, 4 ) );
    SW_EXPECT_EQUAL( 4u, MaterialPacking::alignOffset( 3, 0 ) ); // 크기를 모르면 4 바이트 칸이다

    SW_EXPECT_EQUAL( 16u, MaterialPacking::alignOffset( 4, 8 ) );
    SW_EXPECT_EQUAL( 16u, MaterialPacking::alignOffset( 4, 12 ) );
    SW_EXPECT_EQUAL( 16u, MaterialPacking::alignOffset( 16, 16 ) );
    SW_EXPECT_EQUAL( 32u, MaterialPacking::alignOffset( 17, 16 ) );
    SW_EXPECT_EQUAL( 16u, MaterialPacking::alignOffset( 4, 64 ) );
}

/**
 * @brief [MaterialPackingTest] 채움 칸은 `_swPad` 로 시작하는 이름이다
 */
SW_TEST_CASE( MaterialPackingTest, PaddingMemberIsNamePrefix )
{
    SW_EXPECT_TRUE( MaterialPacking::isPaddingMember( "_swPad" ) );
    SW_EXPECT_TRUE( MaterialPacking::isPaddingMember( "_swPad3" ) );
    SW_EXPECT_FALSE( MaterialPacking::isPaddingMember( "_swPa" ) );
    SW_EXPECT_FALSE( MaterialPacking::isPaddingMember( "x_swPad" ) );
}

/**
 * @brief [MaterialPackingTest] 텍스처 인덱스 필드는 `uint` 이고 이름이 Map 으로 끝나는 멤버뿐이다 — `bool useNormalMap` 은 아니다(결함: Bool 이 Uint 로 묶여 플래그가 텍스처 속성이 되었다)
 */
SW_TEST_CASE( MaterialPackingTest, TextureIndexMemberNeedsUintAndSuffix )
{
    SW_EXPECT_TRUE( MaterialPacking::isTextureIndexMember( "albedoMap", "Uint" ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureIndexMember( "pointFilter", "Uint" ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureIndexMember( "mapScale", "Uint" ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureIndexMember( "albedoMap", "Float" ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureIndexMember( "albedoMap", "" ) );
    SW_EXPECT_FALSE( MaterialPacking::isTextureIndexMember( "useNormalMap", "Bool" ) );
}

/**
 * @brief [MaterialPackingTest] 폴백 원소: 텍스처 인덱스 필드만 kInvalidDescriptorIndex 이고 나머지는 0 이다
 */
SW_TEST_CASE( MaterialPackingTest, FallbackElementMarksOnlyTextureIndices )
{
    using Util = MaterialPackingTestUtil;
    vector<ShaderVariableInfo> listMember;
    listMember.push_back( Util::makeMember( "albedoMap", "Uint", 0, 4 ) );
    listMember.push_back( Util::makeMember( "pointFilter", "Uint", 4, 4 ) );
    listMember.push_back( Util::makeMember( "normalMap", "Uint", 8, 4 ) );
    listMember.push_back( Util::makeMember( "useShadowMap", "Bool", 12, 4 ) );
    listMember.push_back( Util::makeMember( "tint", "Float4", 16, 16 ) );

    const vector<uint8> bytes = MaterialPacking::makeFallbackElement( listMember, 32 );
    SW_REQUIRE_EQUAL( size_t{ 32 }, bytes.size() );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( bytes, 0 ) );
    SW_EXPECT_EQUAL( 0u, Util::readUint( bytes, 4 ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( bytes, 8 ) );
    SW_EXPECT_EQUAL( 0u, Util::readUint( bytes, 12 ) );
    SW_EXPECT_EQUAL( 0u, Util::readUint( bytes, 16 ) );
}

/**
 * @brief [MaterialPackingTest] 폴백 원소의 경계: 끝에 꼭 맞는 필드는 쓰고, 1 바이트라도 넘치는 필드는 쓰지 않는다
 */
SW_TEST_CASE( MaterialPackingTest, FallbackElementStaysInsideStride )
{
    using Util = MaterialPackingTestUtil;
    vector<ShaderVariableInfo> listMember;
    listMember.push_back( Util::makeMember( "lastMap", "Uint", 12, 4 ) );

    const vector<uint8> bytesFit = MaterialPacking::makeFallbackElement( listMember, 16 );
    SW_REQUIRE_EQUAL( size_t{ 16 }, bytesFit.size() );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( bytesFit, 12 ) );

    const vector<uint8> bytesShort = MaterialPacking::makeFallbackElement( listMember, 15 );
    SW_REQUIRE_EQUAL( size_t{ 15 }, bytesShort.size() );
    for ( const uint8 byte : bytesShort )
        SW_EXPECT_EQUAL( uint8{ 0 }, byte );

    SW_EXPECT_TRUE( MaterialPacking::makeFallbackElement( listMember, 0 ).empty() );
}

/**
 * @brief [MaterialPackingTest] 불리언 글: true · 1 · yes · on 과 false · 0 · no · off 를 대소문자 · 앞뒤 공백과 상관없이 읽는다
 */
SW_TEST_CASE( MaterialPackingTest, ParseBoolTokenReadsKnownWords )
{
    const utf8* const arrTrueToken[]  = { "true", "1", "yes", "on", "TRUE", " On " };
    const utf8* const arrFalseToken[] = { "false", "0", "no", "off", "Off", " no " };
    for ( const utf8* pToken : arrTrueToken )
    {
        bool bValue{ false };
        SW_EXPECT_TRUE( MaterialPacking::parseBoolToken( pToken, "_test", "Material.BooleanValueUnreadable", bValue ) );
        SW_EXPECT_TRUE( bValue );
    }
    for ( const utf8* pToken : arrFalseToken )
    {
        bool bValue{ true };
        SW_EXPECT_TRUE( MaterialPacking::parseBoolToken( pToken, "_test", "Material.BooleanValueUnreadable", bValue ) );
        SW_EXPECT_FALSE( bValue );
    }
}

/**
 * @brief [MaterialPackingTest] 불리언이 아닌 글 · 빈 글은 받은 id 로 오류를 남기고 false 이며 출력은 그대로다(대신할 값이 없다)
 */
SW_TEST_CASE( MaterialPackingTest, ParseBoolTokenRejectsOtherText )
{
    SW_EXPECT_LOG( "Material.BooleanValueUnreadable" );
    const utf8* const arrBadToken[] = { "ture", "2", "", "   " };
    for ( const utf8* pToken : arrBadToken )
    {
        bool bTrueValue{ true };
        SW_EXPECT_FALSE( MaterialPacking::parseBoolToken( pToken, "_test", "Material.BooleanValueUnreadable", bTrueValue ) );
        SW_EXPECT_TRUE( bTrueValue );
        bool bFalseValue{ false };
        SW_EXPECT_FALSE( MaterialPacking::parseBoolToken( pToken, "_test", "Material.BooleanValueUnreadable", bFalseValue ) );
        SW_EXPECT_FALSE( bFalseValue );
    }
}

/**
 * @brief [MaterialPackingTest] 묶기는 오프셋 자리까지 버퍼를 늘리고 비어 있던 셰이더 타입 · 크기를 채우며, 더 큰 버퍼는 줄이지 않는다
 */
SW_TEST_CASE( MaterialPackingTest, PackGrowsBufferAndFillsLayout )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Float, "2.5" );
    prop._offset          = 8;

    vector<uint8> buffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_REQUIRE_EQUAL( size_t{ 12 }, buffer.size() );
    SW_EXPECT_NEAR( 2.5f, Util::readFloat( buffer, 8 ), 1e-6f );
    SW_EXPECT_EQUAL( 0u, Util::readUint( buffer, 0 ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float, prop._shaderType );
    SW_EXPECT_EQUAL( 4u, prop._size );

    vector<uint8> largeBuffer( 64, 0 );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, largeBuffer ) );
    SW_EXPECT_EQUAL( size_t{ 64 }, largeBuffer.size() );
}

/**
 * @brief [MaterialPackingTest] float 값을 바로 묶은 바이트는 그 값의 글을 묶은 바이트와 비트까지 같다(종류마다, 끝자리 값 포함)
 */
SW_TEST_CASE( MaterialPackingTest, PackFloatsMatchesPackedText )
{
    using Util                              = MaterialPackingTestUtil;
    const MaterialPropertyType arrTypes[]   = { MaterialPropertyType::Float, MaterialPropertyType::Float2, MaterialPropertyType::Float3, MaterialPropertyType::Float4, MaterialPropertyType::Color, MaterialPropertyType::Range };
    const float32              arrValue[4]  = { 0.1f, -1.17549435e-38f, 16777216.0f, 3.14159274f };
    const float32              arrScalar[1] = { 0.30000001f };
    for ( const MaterialPropertyType type : arrTypes )
    {
        MaterialProperty prop = Util::makeProperty( type, "" );
        prop._offset          = 8;
        prop._min             = 0.0f;
        prop._max             = 0.25f;

        const bool     bVectorShape = type != MaterialPropertyType::Float && type != MaterialPropertyType::Range;
        const uint32   count        = bVectorShape ? 4u : 1u;
        const float32* pValue       = bVectorShape ? arrValue : arrScalar;

        vector<uint8> direct( 64, 0xCD );
        vector<uint8> textBuffer( 64, 0xCD );
        SW_REQUIRE_TRUE( MaterialPacking::packFloatsIntoBuffer( prop, pValue, count, direct ) );
        SW_REQUIRE_TRUE( MaterialPacking::packValueIntoBuffer( prop, MaterialPacking::formatFloats( pValue, count ), textBuffer ) );
        SW_EXPECT_EQUAL( 0, Memory::compare( direct.data(), textBuffer.data(), direct.size() ) );
    }

    // 색에 스칼라 하나를 쓰면 글과 같이 나머지는 0, 알파는 1 이다.
    MaterialProperty color = Util::makeProperty( MaterialPropertyType::Color, "" );
    vector<uint8>    direct( 16, 0xCD );
    vector<uint8>    textBuffer( 16, 0xCD );
    SW_REQUIRE_TRUE( MaterialPacking::packFloatsIntoBuffer( color, arrScalar, 1, direct ) );
    SW_REQUIRE_TRUE( MaterialPacking::packValueIntoBuffer( color, MaterialPacking::formatFloats( arrScalar, 1 ), textBuffer ) );
    SW_EXPECT_EQUAL( 0, Memory::compare( direct.data(), textBuffer.data(), direct.size() ) );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( direct, 12 ), 1e-6f );

    // 글로 읽어야 뜻이 같은 종류(불리언)와 글이 거절하는 값(무한)은 글 길을 지난다.
    MaterialProperty boolProp = Util::makeProperty( MaterialPropertyType::Bool, "" );
    const float32    one      = 1.0f;
    vector<uint8>    boolBuffer;
    SW_REQUIRE_TRUE( MaterialPacking::packFloatsIntoBuffer( boolProp, &one, 1, boolBuffer ) );
    SW_EXPECT_EQUAL( 1u, Util::readUint( boolBuffer, 0 ) );

    MaterialProperty floatProp = Util::makeProperty( MaterialPropertyType::Float, "" );
    const float32    infinite  = std::numeric_limits<float32>::infinity();
    vector<uint8>    infiniteBuffer;
    SW_EXPECT_LOG( "Material.VectorNumberUnreadable" );
    SW_EXPECT_FALSE( MaterialPacking::packFloatsIntoBuffer( floatProp, &infinite, 1, infiniteBuffer ) );
}

/**
 * @brief [MaterialPackingTest] 숫자 글은 공백 · 탭 · 쉼표로 나뉘고, 모자란 칸은 0 이며, 빈 글은 모두 0 이다
 */
SW_TEST_CASE( MaterialPackingTest, PackNumericSplitsTokensAndZeroFills )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Float3, "1,2\t3" );
    vector<uint8>    buffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_REQUIRE_EQUAL( size_t{ 12 }, buffer.size() );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 0 ), 1e-6f );
    SW_EXPECT_NEAR( 2.0f, Util::readFloat( buffer, 4 ), 1e-6f );
    SW_EXPECT_NEAR( 3.0f, Util::readFloat( buffer, 8 ), 1e-6f );

    MaterialProperty shortProp = Util::makeProperty( MaterialPropertyType::Float4, "1 2" );
    vector<uint8>    shortBuffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( shortProp, shortBuffer ) );
    SW_EXPECT_NEAR( 2.0f, Util::readFloat( shortBuffer, 4 ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( shortBuffer, 8 ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( shortBuffer, 12 ), 1e-6f ); // 색이 아니면 넷째 칸도 0 이다

    MaterialProperty emptyProp = Util::makeProperty( MaterialPropertyType::Int2, "" );
    vector<uint8>    emptyBuffer( 8, Util::kUntouchedByte );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( emptyProp, emptyBuffer ) );
    SW_EXPECT_EQUAL( 0, Util::readInt( emptyBuffer, 0 ) );
    SW_EXPECT_EQUAL( 0, Util::readInt( emptyBuffer, 4 ) );
}

/**
 * @brief [MaterialPackingTest] 행렬은 열여섯 칸을 모두 쓴다
 */
SW_TEST_CASE( MaterialPackingTest, PackMatrixWritesSixteenWords )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Float4x4, "1 0 0 0 0 1 0 0 0 0 1 0 5 6 7 8" );
    vector<uint8>    buffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_REQUIRE_EQUAL( size_t{ 64 }, buffer.size() );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 0 ), 1e-6f );
    SW_EXPECT_NEAR( 5.0f, Util::readFloat( buffer, 48 ), 1e-6f );
    SW_EXPECT_NEAR( 8.0f, Util::readFloat( buffer, 60 ), 1e-6f );
}

/**
 * @brief [MaterialPackingTest] 색은 알파를 적지 않으면 1 이다 — 색의 셰이더 타입이 Float4 여도(결함: 셰이더 타입으로 색을 가려 실제 경로에서 알파가 0 이었다)
 */
SW_TEST_CASE( MaterialPackingTest, PackColorDefaultsAlphaToOne )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Color, "1 0.5 0.25" );
    vector<uint8>    buffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( MaterialPropertyType::Float4, prop._shaderType );
    SW_REQUIRE_EQUAL( size_t{ 16 }, buffer.size() );
    SW_EXPECT_NEAR( 0.25f, Util::readFloat( buffer, 8 ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 12 ), 1e-6f );

    MaterialProperty explicitProp = Util::makeProperty( MaterialPropertyType::Color, "1 0 0 0.5" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( explicitProp, buffer ) );
    SW_EXPECT_NEAR( 0.5f, Util::readFloat( buffer, 12 ), 1e-6f );

    MaterialProperty emptyProp = Util::makeProperty( MaterialPropertyType::Color, "" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( emptyProp, buffer ) );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( buffer, 0 ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 12 ), 1e-6f );
}

/**
 * @brief [MaterialPackingTest] 셰이더가 Float3 로 받는 색은 세 칸만 쓴다 — 뒤 칸을 알파로 덮지 않는다
 */
SW_TEST_CASE( MaterialPackingTest, PackColorIntoFloat3LeavesNextWord )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Color, "1 0 0" );
    prop._shaderType      = MaterialPropertyType::Float3;
    vector<uint8> buffer( 16, Util::kUntouchedByte );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 12u, prop._size );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 0 ), 1e-6f );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 12 ) );
}

/**
 * @brief [MaterialPackingTest] 못 읽는 칸이 하나라도 있으면 오류를 남기고 false 이며 버퍼는 앞 칸까지 그대로다
 */
SW_TEST_CASE( MaterialPackingTest, PackNumericFailureLeavesBuffer )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.VectorNumberUnreadable" );
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Float3, "1 x 3" );
    vector<uint8>    buffer( 12, Util::kUntouchedByte );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 8 ) );

    MaterialProperty fractionProp = Util::makeProperty( MaterialPropertyType::Uint, "1.5" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( fractionProp, buffer ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );
}

/**
 * @brief [MaterialPackingTest] 정수 칸의 경계: Uint · Int 의 양 끝은 그대로 읽히고, 넘치는 값은 잘리지 않고 거절된다(결함: 4294967296 이 Uint 칸에서 0 이 되었다)
 */
SW_TEST_CASE( MaterialPackingTest, PackIntegerRangeBoundaries )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.VectorNumberUnreadable" );
    vector<uint8> buffer( 4, Util::kUntouchedByte );

    MaterialProperty uintMax = Util::makeProperty( MaterialPropertyType::Uint, "4294967295" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( uintMax, buffer ) );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, Util::readUint( buffer, 0 ) );

    MaterialProperty intMin = Util::makeProperty( MaterialPropertyType::Int, "-2147483648" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( intMin, buffer ) );
    SW_EXPECT_EQUAL( MathUtil::kMinInt32, Util::readInt( buffer, 0 ) );

    MaterialProperty uintOver = Util::makeProperty( MaterialPropertyType::Uint, "4294967296" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( uintOver, buffer ) );
    MaterialProperty uintNegative = Util::makeProperty( MaterialPropertyType::Uint, "-1" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( uintNegative, buffer ) );
    MaterialProperty intOver = Util::makeProperty( MaterialPropertyType::Int, "2147483648" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( intOver, buffer ) );
    SW_EXPECT_EQUAL( MathUtil::kMinInt32, Util::readInt( buffer, 0 ) ); // 거절된 값은 앞 값을 건드리지 않는다
}

/**
 * @brief [MaterialPackingTest] 칸 크기는 리플렉션이 정한다: 셰이더 타입이 칸보다 크면 쓰지 않고 false, 칸이 더 크면 타입만큼만 쓴다
 */
SW_TEST_CASE( MaterialPackingTest, PackRespectsReflectedSlotSize )
{
    using Util = MaterialPackingTestUtil;

    MaterialProperty tooLarge = Util::makeProperty( MaterialPropertyType::Float4, "1 2 3 4" );
    tooLarge._size            = 4;
    vector<uint8> buffer( 16, Util::kUntouchedByte );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( tooLarge, buffer ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );

    MaterialProperty maskTooLarge = Util::makeProperty( MaterialPropertyType::ChannelMask, "RG" );
    maskTooLarge._shaderType      = MaterialPropertyType::Float4;
    maskTooLarge._size            = 4;
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( maskTooLarge, buffer ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );

    MaterialProperty roomy = Util::makeProperty( MaterialPropertyType::Float, "1" );
    roomy._size            = 16;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( roomy, buffer ) );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 0 ), 1e-6f );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 4 ) );
}

/**
 * @brief [MaterialPackingTest] Bool: 셰이더가 float · Range 면 1.0 · 0.0, 아니면 1 · 0 이고, 못 읽는 글은 버퍼를 건드리지 않고 false 다
 */
SW_TEST_CASE( MaterialPackingTest, PackBoolFollowsShaderType )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.BooleanValueUnreadable" );
    vector<uint8> buffer( 4, Util::kUntouchedByte );

    MaterialProperty uintProp = Util::makeProperty( MaterialPropertyType::Bool, "yes" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( uintProp, buffer ) );
    SW_EXPECT_EQUAL( 1u, Util::readUint( buffer, 0 ) );

    MaterialProperty floatProp = Util::makeProperty( MaterialPropertyType::Bool, "on" );
    floatProp._shaderType      = MaterialPropertyType::Float;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( floatProp, buffer ) );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( buffer, 0 ), 1e-6f );

    MaterialProperty rangeProp = Util::makeProperty( MaterialPropertyType::Bool, "off" );
    rangeProp._shaderType      = MaterialPropertyType::Range;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( rangeProp, buffer ) );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( buffer, 0 ), 1e-6f );

    MaterialProperty oneProp = Util::makeProperty( MaterialPropertyType::Bool, "1" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( oneProp, buffer ) );
    MaterialProperty badProp = Util::makeProperty( MaterialPropertyType::Bool, "ture" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( badProp, buffer ) );
    MaterialProperty emptyProp = Util::makeProperty( MaterialPropertyType::Bool, "" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( emptyProp, buffer ) );
    SW_EXPECT_EQUAL( 1u, Util::readUint( buffer, 0 ) );
}

/**
 * @brief [MaterialPackingTest] Range: [min, max] 로 잘리고, min 이 max 보다 작을 때만 자르며, 못 읽는 글 · 빈 글은 0 을 쓰지 않고 false 다
 */
SW_TEST_CASE( MaterialPackingTest, PackRangeClampsAndRejects )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.ParameterNumberUnreadable" );
    vector<uint8> buffer( 4, 0 );

    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Range, "2.5" );
    prop._min             = -1.0f;
    prop._max             = 1.5f;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_NEAR( 1.5f, Util::readFloat( buffer, 0 ), 1e-6f );

    prop._value = "-3";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_NEAR( -1.0f, Util::readFloat( buffer, 0 ), 1e-6f );

    prop._value = "0.25";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_NEAR( 0.25f, Util::readFloat( buffer, 0 ), 1e-6f );

    MaterialProperty noRange = Util::makeProperty( MaterialPropertyType::Range, "5" );
    noRange._min             = 0.0f;
    noRange._max             = 0.0f;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( noRange, buffer ) );
    SW_EXPECT_NEAR( 5.0f, Util::readFloat( buffer, 0 ), 1e-6f );

    MaterialProperty badProp = Util::makeProperty( MaterialPropertyType::Range, "abc" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( badProp, buffer ) );
    MaterialProperty emptyProp = Util::makeProperty( MaterialPropertyType::Range, "" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( emptyProp, buffer ) );
    SW_EXPECT_NEAR( 5.0f, Util::readFloat( buffer, 0 ), 1e-6f );
}

/**
 * @brief [MaterialPackingTest] Enum: 항목 이름이 먼저이고 숫자도 읽으며(16 진 포함), 빈 글은 0, 셰이더 타입(Int · Float · Uint)대로 쓴다
 */
SW_TEST_CASE( MaterialPackingTest, PackEnumReadsNamesAndNumbers )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::Enum, " High " );
    Util::addEnumEntry( prop, "Low", 1 );
    Util::addEnumEntry( prop, "High", 2 );
    vector<uint8> buffer( 4, Util::kUntouchedByte );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 2u, Util::readUint( buffer, 0 ) );

    prop._value = "0x10";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 16u, Util::readUint( buffer, 0 ) );

    prop._value = "";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 0u, Util::readUint( buffer, 0 ) );

    MaterialProperty intProp = Util::makeProperty( MaterialPropertyType::Enum, "-1" );
    intProp._shaderType      = MaterialPropertyType::Int;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( intProp, buffer ) );
    SW_EXPECT_EQUAL( -1, Util::readInt( buffer, 0 ) );

    MaterialProperty floatProp = Util::makeProperty( MaterialPropertyType::Enum, "High" );
    Util::addEnumEntry( floatProp, "High", 2 );
    floatProp._shaderType = MaterialPropertyType::Float;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( floatProp, buffer ) );
    SW_EXPECT_NEAR( 2.0f, Util::readFloat( buffer, 0 ), 1e-6f );
}

/**
 * @brief [MaterialPackingTest] BitFlag: `|` · `,` 로 나눈 토큰을 합치고, 셰이더 타입이 Float 여도 비트를 그대로 쓴다
 */
SW_TEST_CASE( MaterialPackingTest, PackBitFlagCombinesTokens )
{
    using Util            = MaterialPackingTestUtil;
    MaterialProperty prop = Util::makeProperty( MaterialPropertyType::BitFlag, "A | B,4" );
    Util::addEnumEntry( prop, "A", 1 );
    Util::addEnumEntry( prop, "B", 2 );
    vector<uint8> buffer( 4, 0 );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 7u, Util::readUint( buffer, 0 ) );

    prop._shaderType = MaterialPropertyType::Float;
    prop._value      = "A|B";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
    SW_EXPECT_EQUAL( 3u, Util::readUint( buffer, 0 ) );
}

/**
 * @brief [MaterialPackingTest] 모르는 열거 이름 · 플래그 토큰은 경고하고 false 이며 앞 값이 남는다. 등록되지 않은 enum 타입 이름은 숫자로 읽는다
 */
SW_TEST_CASE( MaterialPackingTest, PackEnumRejectsUnknownNames )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.UnknownEnumValue" );
    vector<uint8> buffer( 4, Util::kUntouchedByte );

    MaterialProperty enumProp = Util::makeProperty( MaterialPropertyType::Enum, "Nope" );
    Util::addEnumEntry( enumProp, "Low", 1 );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( enumProp, buffer ) );

    MaterialProperty flagProp = Util::makeProperty( MaterialPropertyType::BitFlag, "A|Nope" );
    Util::addEnumEntry( flagProp, "A", 1 );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( flagProp, buffer ) );
    SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );

    MaterialProperty unregistered = Util::makeProperty( MaterialPropertyType::Enum, "5" );
    unregistered._enumType        = "MaterialPackingTestEnumNeverRegistered";
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( unregistered, buffer ) );
    SW_EXPECT_EQUAL( 5u, Util::readUint( buffer, 0 ) );
}

/**
 * @brief [MaterialPackingTest] ChannelMask 를 읽는다: 빈 글은 네 채널, 글자는 대소문자 없이 비트로, 숫자는 0..15 그대로, Float4 셰이더면 채널마다 1 · 0
 */
SW_TEST_CASE( MaterialPackingTest, PackChannelMaskReadsLettersAndNumbers )
{
    using Util = MaterialPackingTestUtil;
    struct Row
    {
        const utf8* _pValue;
        uint32      _mask;
    };
    const Row arrRow[] = {
        {    "", 0xFu},
        {  "RG", 0x3u},
        {   "a", 0x8u},
        {"rgba", 0xFu},
        {   "5", 0x5u},
        {   "0", 0x0u},
        { "0xF", 0xFu},
        {  "15", 0xFu},
    };
    vector<uint8> buffer( 4, 0 );
    for ( const Row& row : arrRow )
    {
        MaterialProperty prop = Util::makeProperty( MaterialPropertyType::ChannelMask, row._pValue );
        SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
        SW_EXPECT_EQUAL( row._mask, Util::readUint( buffer, 0 ) );
    }

    MaterialProperty floatProp = Util::makeProperty( MaterialPropertyType::ChannelMask, "RB" );
    floatProp._shaderType      = MaterialPropertyType::Float4;
    vector<uint8> floatBuffer;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( floatProp, floatBuffer ) );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( floatBuffer, 0 ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( floatBuffer, 4 ), 1e-6f );
    SW_EXPECT_NEAR( 1.0f, Util::readFloat( floatBuffer, 8 ), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, Util::readFloat( floatBuffer, 12 ), 1e-6f );
}

/**
 * @brief [MaterialPackingTest] ChannelMask 오타 · 넘치는 숫자는 오류를 남기고 false 이며 앞 값이 남는다(결함: 오타가 조용히 네 채널이 되었다)
 */
SW_TEST_CASE( MaterialPackingTest, PackChannelMaskRejectsUnreadableText )
{
    using Util = MaterialPackingTestUtil;
    SW_EXPECT_LOG( "Material.ChannelMaskUnreadable" );
    const utf8* const arrBadValue[] = { "RX", "XYZ", "16", "R G" };
    vector<uint8>     buffer( 4, Util::kUntouchedByte );
    for ( const utf8* pValue : arrBadValue )
    {
        MaterialProperty prop = Util::makeProperty( MaterialPropertyType::ChannelMask, pValue );
        SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( prop, buffer ) );
        SW_EXPECT_EQUAL( Util::kUntouchedWord, Util::readUint( buffer, 0 ) );
    }
}

/**
 * @brief [MaterialPackingTest] 텍스처: 붙은 인덱스가 먼저이고, 없으면 숫자 글, 숫자가 아니거나 넘치는 글이면 kInvalidDescriptorIndex 다 — 0 은 유효한 인덱스다(결함: 넘치는 숫자가 잘려 첫 텍스처가 되었다)
 */
SW_TEST_CASE( MaterialPackingTest, PackTextureIndexPriority )
{
    using Util = MaterialPackingTestUtil;
    vector<uint8> buffer( 4, 0 );

    MaterialProperty unbound = Util::makeProperty( MaterialPropertyType::Texture2D, "" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( unbound, buffer ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( buffer, 0 ) );

    MaterialProperty numeric = Util::makeProperty( MaterialPropertyType::TextureCube, "12" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( numeric, buffer ) );
    SW_EXPECT_EQUAL( 12u, Util::readUint( buffer, 0 ) );

    MaterialProperty bound = Util::makeProperty( MaterialPropertyType::Texture3D, "12" );
    bound._textureIndex    = 7;
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( bound, buffer ) );
    SW_EXPECT_EQUAL( 7u, Util::readUint( buffer, 0 ) );

    MaterialProperty path = Util::makeProperty( MaterialPropertyType::Texture2DArray, "textures/a.png" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( path, buffer ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( buffer, 0 ) );

    MaterialProperty overflow = Util::makeProperty( MaterialPropertyType::Texture2D, "4294967296" );
    SW_REQUIRE_TRUE( MaterialPacking::packPropertyIntoBuffer( overflow, buffer ) );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, Util::readUint( buffer, 0 ) );
}

/**
 * @brief [MaterialPackingTest] 키워드는 버퍼에 들어가지 않고 크기가 0 이 되며, 타입이 Unknown 이면 false 다
 */
SW_TEST_CASE( MaterialPackingTest, PackKeywordAndUnknown )
{
    using Util               = MaterialPackingTestUtil;
    MaterialProperty keyword = Util::makeProperty( MaterialPropertyType::Keyword, "1" );
    keyword._size            = 4;
    vector<uint8> buffer;
    SW_EXPECT_TRUE( MaterialPacking::packPropertyIntoBuffer( keyword, buffer ) );
    SW_EXPECT_EQUAL( 0u, keyword._size );
    SW_EXPECT_TRUE( buffer.empty() );

    MaterialProperty unknown = Util::makeProperty( MaterialPropertyType::Unknown, "1" );
    SW_EXPECT_FALSE( MaterialPacking::packPropertyIntoBuffer( unknown, buffer ) );
}
