#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

// ShaderBindingLayout — 스테이지별 리플렉션을 슬롯으로 합치기(가시성 · CB 멤버 · 구조 버퍼 stride), 이름 조회(canonical 이름), 드로우 전 바인딩 계획.
// 리플렉션 값은 손으로 만든다 — 실제 바이트코드의 리플렉션은 ShaderReflectionData 쪽이 본다.

using namespace sw;

namespace
{
    struct ShaderBindingLayoutTestUtil
    {
        using StageList = vector<pair<ShaderStage, const ShaderReflectionData*>>;

        static void addResource( ShaderReflectionData& reflection, const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint, uint32 bindCount = 1 )
        {
            ShaderReflectedBinding resource{};
            resource._name          = pName;
            resource._type          = pType;
            resource._registerSpace = space;
            resource._bindPoint     = bindPoint;
            resource._bindCount     = bindCount;
            reflection._listResource.push_back( resource );
        }

        static ShaderVariableInfo makeVariable( const utf8* pName, uint32 offset, uint32 size )
        {
            ShaderVariableInfo variable{};
            variable._name   = pName;
            variable._type   = "Float";
            variable._offset = offset;
            variable._size   = size;
            return variable;
        }

        static ShaderBufferInfo& addConstantBuffer( ShaderReflectionData& reflection, const utf8* pName, uint32 bindPoint, uint32 totalSize )
        {
            ShaderBufferInfo buffer{};
            buffer._name      = pName;
            buffer._bindPoint = bindPoint;
            buffer._totalSize = totalSize;
            reflection._listConstantBuffer.push_back( buffer );
            return reflection._listConstantBuffer.back();
        }

        static void addStructuredElement( ShaderReflectionData& reflection, const utf8* pName, uint32 totalSize )
        {
            ShaderBufferInfo element{};
            element._name      = pName;
            element._totalSize = totalSize;
            element._listVariable.push_back( makeVariable( "albedoMap", 0, 4 ) );
            reflection._listStructuredElement.push_back( element );
        }

        static ShaderBindingLayout makeSingleStage( const ShaderReflectionData& reflection )
        {
            StageList listStage;
            listStage.push_back( { ShaderStage::Pixel, &reflection } );
            return ShaderBindingLayout::make( listStage );
        }

        static const ShaderEngineCbMember* findCbMember( const ShaderBindingLayout& layout, const utf8* pName )
        {
            const hashed_string key{ string_view( pName ) };
            for ( const ShaderEngineCbMember& member : layout.getEngineCbMembers() )
            {
                if ( member._valueKey == key )
                    return &member;
            }
            return nullptr;
        }

        static hashed_string name( const utf8* pName ) { return hashed_string{ string_view( pName ) }; }
    };
} // namespace

/**
 * @brief [ShaderBindingLayoutTest] 타입 라벨: DX · SPIR-V 라벨이 같은 종류로 모이고, 모르는 라벨은 Unknown 이다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, KindFromTypeLabel )
{
    SW_EXPECT_EQUAL( ShaderBindingKind::Texture, ShaderBindingLayout::kindFromTypeLabel( "Texture" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::Texture, ShaderBindingLayout::kindFromTypeLabel( "TextureOrSampler" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::Sampler, ShaderBindingLayout::kindFromTypeLabel( "Sampler" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::ConstantBuffer, ShaderBindingLayout::kindFromTypeLabel( "ConstantBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::StructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "StructuredBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::StructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "StorageBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::StructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "ByteAddressBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::RwStructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "UAV" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::RwStructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "RWStructuredBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::RwStructuredBuffer, ShaderBindingLayout::kindFromTypeLabel( "RWByteAddressBuffer" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::RwTexture, ShaderBindingLayout::kindFromTypeLabel( "RWTexture" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::Unknown, ShaderBindingLayout::kindFromTypeLabel( "OtherResource" ) );
    SW_EXPECT_EQUAL( ShaderBindingKind::Unknown, ShaderBindingLayout::kindFromTypeLabel( "" ) );
}

/**
 * @brief [ShaderBindingLayoutTest] 스테이지가 없거나 리플렉션이 nullptr 이면 빈 레이아웃이다. 모르는 라벨의 리소스는 슬롯이 되지 않는다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, EmptyInputsMakeEmptyLayout )
{
    using Util = ShaderBindingLayoutTestUtil;

    const ShaderBindingLayout noStage = ShaderBindingLayout::make( Util::StageList{} );
    SW_EXPECT_TRUE( noStage.isEmpty() );
    SW_EXPECT_EQUAL( 0u, noStage.getEngineCbSize() );

    const ShaderReflectionData* pNoReflection{ nullptr };
    Util::StageList             listNull;
    listNull.push_back( { ShaderStage::Vertex, pNoReflection } );
    SW_EXPECT_TRUE( ShaderBindingLayout::make( listNull ).isEmpty() );

    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_Strange", "OtherResource", 0, 0 );
    SW_EXPECT_TRUE( Util::makeSingleStage( reflection ).isEmpty() );
}

/**
 * @brief [ShaderBindingLayoutTest] (종류, space, register) 가 같으면 스테이지를 넘어 한 슬롯이고 가시성만 합쳐진다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, SameLocationMergesVisibility )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData vertex;
    ShaderReflectionData pixel;
    Util::addResource( vertex, "g_Tex", "Texture", 0, 3 );
    Util::addResource( pixel, "g_Tex", "Texture", 0, 3 );

    Util::StageList listStage;
    listStage.push_back( { ShaderStage::Vertex, &vertex } );
    listStage.push_back( { ShaderStage::Pixel, &pixel } );
    const ShaderBindingLayout layout = ShaderBindingLayout::make( listStage );

    SW_REQUIRE_EQUAL( size_t{ 1 }, layout.getSlots().size() );
    const uint8 expected = static_cast<uint8>( static_cast<uint8>( ShaderStageFlag::Vertex ) | static_cast<uint8>( ShaderStageFlag::Pixel ) );
    SW_EXPECT_EQUAL( expected, static_cast<uint8>( layout.getSlots()[0]._visibility ) );
}

/**
 * @brief [ShaderBindingLayoutTest] 종류 · space · register 중 하나라도 다르면 다른 슬롯이다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, DifferentLocationKeepsSeparateSlots )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_A", "Texture", 0, 0 );
    Util::addResource( reflection, "g_B", "Texture", 1, 0 );
    Util::addResource( reflection, "g_C", "Texture", 0, 1 );
    Util::addResource( reflection, "g_D", "Sampler", 0, 0 );

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );
    SW_REQUIRE_EQUAL( size_t{ 4 }, layout.getSlots().size() );
    const ShaderBindingSlot* pSlot = layout.find( Util::name( "g_B" ) );
    SW_REQUIRE_NOT_NULL( pSlot );
    SW_EXPECT_EQUAL( 1u, pSlot->_space );
    SW_EXPECT_EQUAL( 0u, pSlot->_registerIndex );
    SW_EXPECT_EQUAL( ShaderBindingKind::Texture, pSlot->_kind );
}

/**
 * @brief [ShaderBindingLayoutTest] 배열 크기는 리소스의 것이지만 상수 버퍼는 언제나 1 이다. 리소스 목록의 CB 는 CB 목록의 슬롯에 합쳐져 멤버를 잃지 않는다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, ArrayCountAndConstantBufferMerge )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_Array", "Texture", 0, 4, 8 );
    Util::addResource( reflection, "g_Unbounded", "Texture", 1, 0, 0 );
    Util::addResource( reflection, "PassCB", "ConstantBuffer", 0, 0, 5 );
    Util::addConstantBuffer( reflection, "PassCB", 0, 32 )._listVariable.push_back( Util::makeVariable( "g_Time", 0, 4 ) );

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );
    SW_REQUIRE_EQUAL( size_t{ 3 }, layout.getSlots().size() );

    const ShaderBindingSlot* pArray = layout.find( Util::name( "g_Array" ) );
    SW_REQUIRE_NOT_NULL( pArray );
    SW_EXPECT_EQUAL( 8u, pArray->_arrayCount );
    const ShaderBindingSlot* pUnbounded = layout.find( Util::name( "g_Unbounded" ) );
    SW_REQUIRE_NOT_NULL( pUnbounded );
    SW_EXPECT_EQUAL( 0u, pUnbounded->_arrayCount );

    const ShaderBindingSlot* pCb = layout.find( Util::name( "PassCB" ) );
    SW_REQUIRE_NOT_NULL( pCb );
    SW_EXPECT_EQUAL( 1u, pCb->_arrayCount );
    SW_EXPECT_EQUAL( 32u, pCb->_cbTotalSize );
    SW_EXPECT_EQUAL( size_t{ 1 }, pCb->_listCbMember.size() );
}

/**
 * @brief [ShaderBindingLayoutTest] CB 멤버 · 크기는 먼저 알린 스테이지의 것이고, 비어 있던 쪽만 뒤 스테이지가 채운다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, ConstantBufferMembersFillOnlyEmpty )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData first;
    ShaderReflectionData second;
    Util::addConstantBuffer( first, "EmptyFirst", 0, 0 );
    Util::addConstantBuffer( second, "EmptyFirst", 0, 48 )._listVariable.push_back( Util::makeVariable( "g_A", 0, 4 ) );
    Util::addConstantBuffer( first, "FullFirst", 1, 16 )._listVariable.push_back( Util::makeVariable( "g_B", 0, 4 ) );
    ShaderBufferInfo& fullSecond = Util::addConstantBuffer( second, "FullFirst", 1, 64 );
    fullSecond._listVariable.push_back( Util::makeVariable( "g_C", 0, 4 ) );
    fullSecond._listVariable.push_back( Util::makeVariable( "g_D", 4, 4 ) );

    Util::StageList listStage;
    listStage.push_back( { ShaderStage::Vertex, &first } );
    listStage.push_back( { ShaderStage::Pixel, &second } );
    const ShaderBindingLayout layout = ShaderBindingLayout::make( listStage );

    const ShaderBindingSlot* pEmptyFirst = layout.find( Util::name( "EmptyFirst" ) );
    SW_REQUIRE_NOT_NULL( pEmptyFirst );
    SW_EXPECT_EQUAL( 48u, pEmptyFirst->_cbTotalSize );
    SW_EXPECT_EQUAL( size_t{ 1 }, pEmptyFirst->_listCbMember.size() );

    const ShaderBindingSlot* pFullFirst = layout.find( Util::name( "FullFirst" ) );
    SW_REQUIRE_NOT_NULL( pFullFirst );
    SW_EXPECT_EQUAL( 16u, pFullFirst->_cbTotalSize );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pFullFirst->_listCbMember.size() );
    SW_EXPECT_TRUE( pFullFirst->_listCbMember[0]._name == "g_B" );
}

/**
 * @brief [ShaderBindingLayoutTest] 구조 버퍼 stride 는 다른 스테이지의 원소에서도 이름으로 붙는다(RW 포함). 크기 0 · 다른 이름 · 구조 버퍼가 아닌 슬롯은 붙지 않고, 먼저 붙은 값이 남는다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, StructuredStrideAttachesByName )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData vertex;
    Util::addResource( vertex, "g_Read", "StructuredBuffer", 0, 4 );
    Util::addResource( vertex, "g_Write", "UAV", 0, 0 );
    Util::addResource( vertex, "g_Zero", "StructuredBuffer", 0, 5 );
    Util::addResource( vertex, "g_NotBuffer", "Texture", 0, 6 );

    ShaderReflectionData pixel;
    Util::addStructuredElement( pixel, "g_Read", 48 );
    Util::addStructuredElement( pixel, "g_Read", 96 );
    Util::addStructuredElement( pixel, "g_Write", 16 );
    Util::addStructuredElement( pixel, "g_Zero", 0 );
    Util::addStructuredElement( pixel, "g_NotBuffer", 32 );
    Util::addStructuredElement( pixel, "g_Other", 32 );

    Util::StageList listStage;
    listStage.push_back( { ShaderStage::Vertex, &vertex } );
    listStage.push_back( { ShaderStage::Pixel, &pixel } );
    const ShaderBindingLayout layout = ShaderBindingLayout::make( listStage );

    const ShaderBindingSlot* pRead = layout.find( Util::name( "g_Read" ) );
    SW_REQUIRE_NOT_NULL( pRead );
    SW_EXPECT_EQUAL( 48u, pRead->_elementStride );
    SW_EXPECT_EQUAL( size_t{ 1 }, pRead->_listElementMember.size() );

    const ShaderBindingSlot* pWrite = layout.find( Util::name( "g_Write" ) );
    SW_REQUIRE_NOT_NULL( pWrite );
    SW_EXPECT_EQUAL( 16u, pWrite->_elementStride );

    const ShaderBindingSlot* pZero = layout.find( Util::name( "g_Zero" ) );
    SW_REQUIRE_NOT_NULL( pZero );
    SW_EXPECT_EQUAL( 0u, pZero->_elementStride );
    SW_EXPECT_TRUE( pZero->_listElementMember.empty() );

    const ShaderBindingSlot* pNotBuffer = layout.find( Util::name( "g_NotBuffer" ) );
    SW_REQUIRE_NOT_NULL( pNotBuffer );
    SW_EXPECT_EQUAL( 0u, pNotBuffer->_elementStride );

    SW_EXPECT_TRUE( layout.find( Util::name( "g_Other" ) ) == nullptr );
}

/**
 * @brief [ShaderBindingLayoutTest] 선언 이름과 `g_` 를 뗀 canonical 이름 모두로 찾는다. `Index` 접미사는 슬롯 이름에서 떼지 않고, `g_` 만인 이름은 그대로다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, FindByDeclaredAndCanonicalName )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_SwMaterials", "StructuredBuffer", 0, 5 );
    Util::addResource( reflection, "ShadowMap", "Texture", 0, 1 );
    Util::addResource( reflection, "g_FooIndex", "Texture", 0, 2 );
    Util::addResource( reflection, "g_", "Texture", 0, 3 );

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );

    const ShaderBindingSlot* pDeclared  = layout.find( Util::name( "g_SwMaterials" ) );
    const ShaderBindingSlot* pCanonical = layout.find( Util::name( "SwMaterials" ) );
    SW_REQUIRE_NOT_NULL( pDeclared );
    SW_EXPECT_TRUE( pDeclared == pCanonical );

    SW_EXPECT_TRUE( layout.find( Util::name( "ShadowMap" ) ) != nullptr );
    SW_EXPECT_TRUE( layout.find( Util::name( "FooIndex" ) ) != nullptr );
    SW_EXPECT_TRUE( layout.find( Util::name( "Foo" ) ) == nullptr );
    SW_EXPECT_TRUE( layout.find( Util::name( "g_" ) ) != nullptr );
    SW_EXPECT_TRUE( layout.find( Util::name( "NoSuchBinding" ) ) == nullptr );
}

/**
 * @brief [ShaderBindingLayoutTest] 같은 이름이 두 자리에 있으면 조회는 먼저 것을 찾는다. canonical 이름이 겹쳐도 선언 이름으로 든 슬롯이 밀리지 않는다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, DuplicateNamesKeepFirstSlot )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_Dup", "Texture", 0, 1 );
    Util::addResource( reflection, "g_Dup", "Texture", 0, 2 );
    Util::addResource( reflection, "Plain", "Texture", 0, 3 );
    Util::addResource( reflection, "g_Plain", "Texture", 0, 4 );

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );
    SW_EXPECT_EQUAL( size_t{ 4 }, layout.getSlots().size() );

    const ShaderBindingSlot* pDup = layout.find( Util::name( "g_Dup" ) );
    SW_REQUIRE_NOT_NULL( pDup );
    SW_EXPECT_EQUAL( 1u, pDup->_registerIndex );

    const ShaderBindingSlot* pPlain = layout.find( Util::name( "Plain" ) );
    SW_REQUIRE_NOT_NULL( pPlain );
    SW_EXPECT_EQUAL( 3u, pPlain->_registerIndex );
    const ShaderBindingSlot* pGlobalPlain = layout.find( Util::name( "g_Plain" ) );
    SW_REQUIRE_NOT_NULL( pGlobalPlain );
    SW_EXPECT_EQUAL( 4u, pGlobalPlain->_registerIndex );
}

/**
 * @brief [ShaderBindingLayoutTest] 엔진 CB 크기: 머티리얼 CB 는 빠지고, CB 마다의 크기와 멤버 끝 중 큰 값의 최댓값이다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, EngineCbSizeSkipsMaterialAndTakesMaximum )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addConstantBuffer( reflection, shaderslot::cbname::kMaterial, 1, 1024 )._listVariable.push_back( Util::makeVariable( "g_MaterialValue", 0, 4 ) );
    Util::addConstantBuffer( reflection, "PassCB", 0, 16 )._listVariable.push_back( Util::makeVariable( "g_Past", 32, 16 ) ); // 크기(16)보다 멤버 끝(48)이 크다

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );
    SW_EXPECT_EQUAL( 48u, layout.getEngineCbSize() );
    SW_EXPECT_TRUE( Util::findCbMember( layout, "g_MaterialValue" ) == nullptr );

    const ShaderEngineCbMember* pPast = Util::findCbMember( layout, "g_Past" );
    SW_REQUIRE_NOT_NULL( pPast );
    SW_EXPECT_EQUAL( 32u, pPast->_offset );
    SW_EXPECT_EQUAL( 16u, pPast->_size );

    ShaderReflectionData sizeWins;
    Util::addConstantBuffer( sizeWins, "PassCB", 0, 256 )._listVariable.push_back( Util::makeVariable( "g_Time", 0, 4 ) );
    Util::addConstantBuffer( sizeWins, "Other", 2, 64 );
    SW_EXPECT_EQUAL( 256u, Util::makeSingleStage( sizeWins ).getEngineCbSize() );
}

/**
 * @brief [ShaderBindingLayoutTest] 자동 인덱스 키: 이름에 Index 가 있는 4 바이트 멤버만이고, `g_` 와 끝의 Index 를 뗀 이름이다
 */
SW_TEST_CASE( ShaderBindingLayoutTest, AutoIndexKeyRules )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    ShaderBufferInfo&    cb = Util::addConstantBuffer( reflection, "PassCB", 0, 64 );
    cb._listVariable.push_back( Util::makeVariable( "g_ShadowMapIndex", 0, 4 ) );
    cb._listVariable.push_back( Util::makeVariable( "g_IndexCount", 4, 4 ) );
    cb._listVariable.push_back( Util::makeVariable( "g_Index", 8, 4 ) );
    cb._listVariable.push_back( Util::makeVariable( "g_WideIndex", 16, 8 ) );
    cb._listVariable.push_back( Util::makeVariable( "g_Time", 24, 4 ) );

    const ShaderBindingLayout layout = Util::makeSingleStage( reflection );

    const ShaderEngineCbMember* pShadow = Util::findCbMember( layout, "g_ShadowMapIndex" );
    SW_REQUIRE_NOT_NULL( pShadow );
    SW_EXPECT_TRUE( pShadow->_autoIndexKey == Util::name( "ShadowMap" ) );

    const ShaderEngineCbMember* pCount = Util::findCbMember( layout, "g_IndexCount" );
    SW_REQUIRE_NOT_NULL( pCount );
    SW_EXPECT_TRUE( pCount->_autoIndexKey == Util::name( "IndexCount" ) );

    const ShaderEngineCbMember* pOnlyIndex = Util::findCbMember( layout, "g_Index" );
    SW_REQUIRE_NOT_NULL( pOnlyIndex );
    SW_EXPECT_TRUE( pOnlyIndex->_autoIndexKey == Util::name( "Index" ) ); // 글자 다섯 뿐이면 접미사로 떼지 않는다

    const ShaderEngineCbMember* pWide = Util::findCbMember( layout, "g_WideIndex" );
    SW_REQUIRE_NOT_NULL( pWide );
    SW_EXPECT_TRUE( pWide->_autoIndexKey.empty() );

    const ShaderEngineCbMember* pTime = Util::findCbMember( layout, "g_Time" );
    SW_REQUIRE_NOT_NULL( pTime );
    SW_EXPECT_TRUE( pTime->_autoIndexKey.empty() );
}

/**
 * @brief [ShaderBindingLayoutTest] 리소스 바인딩 계획: 텍스처와 읽기 구조 버퍼만 canonical 키로 들어간다(RW · 샘플러 · CB 는 아니다)
 */
SW_TEST_CASE( ShaderBindingLayoutTest, ResourceBindOpsCoverReadResources )
{
    using Util = ShaderBindingLayoutTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, "g_SwMaterials", "StructuredBuffer", 0, 5 );
    Util::addResource( reflection, "ShadowMap", "Texture", 0, 7 );
    Util::addResource( reflection, "g_Write", "UAV", 0, 0 );
    Util::addResource( reflection, "g_RwImage", "RWTexture", 0, 1 );
    Util::addResource( reflection, "g_Sampler", "Sampler", 0, 0 );
    Util::addResource( reflection, "PassCB", "ConstantBuffer", 0, 0 );

    const ShaderBindingLayout           layout = Util::makeSingleStage( reflection );
    const vector<ShaderResourceBindOp>& listOp = layout.getResourceBindOps();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listOp.size() );

    SW_EXPECT_TRUE( listOp[0]._lookupKey == Util::name( "SwMaterials" ) );
    SW_EXPECT_EQUAL( 5u, listOp[0]._registerIndex );
    SW_EXPECT_EQUAL( ShaderBindingKind::StructuredBuffer, listOp[0]._kind );

    SW_EXPECT_TRUE( listOp[1]._lookupKey == Util::name( "ShadowMap" ) );
    SW_EXPECT_EQUAL( 7u, listOp[1]._registerIndex );
    SW_EXPECT_EQUAL( ShaderBindingKind::Texture, listOp[1]._kind );
}
