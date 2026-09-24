#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/Rpc/ReflectionRpc.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션 직렬화 — Binary/JSON/XML 왕복 · 버전 · 스키마 마이그레이션 · 골든 출력 · 딥카피.
// 골든 비교 헬퍼는 아래 익명 네임스페이스에 있다.

namespace
{
    /** @brief 골든 테스트용 고정 상태의 NestedContainerActor 를 만듭니다. */
    sw::NestedContainerActor makeGoldenNestedActor()
    {
        sw::NestedContainerActor src;
        src._grid = {
            { 1, 2 },
            { 3, 4, 5 }
        };
        src._namedRows["a"] = { 1.0f, 2.5f };
        src._namedRows["b"] = { -3.25f };
        // 중첩 맵 / 구조체 원소도 골든에 포함해 표기 변화를 놓치지 않게 한다.
        src._nestedMap["out"]["x"] = 7;
        src._listInner.push_back( sw::NestedInner{ 11 } );
        src._mapInner["m"]._x = 33;
        src._inner._x         = 42;
        return src;
    }

    /** @brief 바이트 버퍼를 소문자 16진 문자열로 인코딩합니다. */
    sw::string toHexString( const sw::vector<uint8>& bytes )
    {
        static constexpr utf8 kDigit[] = "0123456789abcdef";
        sw::string            out;
        out.reserve( bytes.size() * 2 );
        for ( uint8 byteValue : bytes )
        {
            out.push_back( kDigit[byteValue >> 4] );
            out.push_back( kDigit[byteValue & 0x0F] );
        }
        return out;
    }

    /** @brief 골든 문자열 비교 — 불일치 시 양쪽을 stdout 에 찍고 실패로 이어지도록 false 반환. */
    bool goldenEq( const utf8* pLabel, const sw::string& actual, const utf8* pExpected )
    {
        if ( actual == pExpected )
            return true;
        std::fprintf( stdout, "[golden %s]\n---expected---\n%s\n---actual---\n%s\n", pLabel, pExpected, actual.c_str() );
        std::fflush( stdout );
        return false;
    }

    /** @brief 한 인스턴스의 5개 골든(Json/Xml/Bin/JsonVer/BinVer)을 검증합니다. */
    bool checkGoldenSet( const utf8* pLabel, const void* pInstance, const sw::TypeInfo& typeInfo,
                         const utf8* pJson, const utf8* pXml, const utf8* pBinHex, const utf8* pJsonVer, const utf8* pBinVerHex )
    {
        sw::vector<uint8> bin;
        sw::BinarySerializer::serialize( pInstance, typeInfo, bin );
        sw::vector<uint8> binVer;
        sw::BinarySerializer::serializeVersioned( 7, pInstance, typeInfo, binVer );

        bool bOk = true;
        bOk &= goldenEq( pLabel, sw::JsonSerializer::serialize( pInstance, typeInfo ), pJson );
        bOk &= goldenEq( pLabel, sw::XmlSerializer::serialize( pInstance, typeInfo ), pXml );
        bOk &= goldenEq( pLabel, toHexString( bin ), pBinHex );
        bOk &= goldenEq( pLabel, sw::JsonSerializer::serializeVersioned( 7, pInstance, typeInfo ), pJsonVer );
        bOk &= goldenEq( pLabel, toHexString( binVer ), pBinVerHex );
        return bOk;
    }
} // namespace

/**
 * @brief [ReflectionSerializationTest] 오브젝트 diff 직렬화 델타
 */
SW_TEST_CASE( ReflectionSerializationTest, ObjectDiffSerializationDelta )
{
    const sw::TypeInfo* info = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::DummyActor" ) );
    SW_EXPECT_TRUE( info != nullptr );
    if ( info != nullptr )
    {
        sw::DummyActor cdoActor;
        sw::DummyActor modActor;
        modActor._hp = 999;

        sw::vector<uint8> diffBuf;
        bool              diffOk = sw::ObjectDiffSerializer::serializeDiff( diffBuf, &cdoActor, &modActor, *info );
        SW_EXPECT_TRUE( diffOk );
        SW_EXPECT_FALSE( diffBuf.empty() );

        sw::DummyActor restored = cdoActor;
        SW_EXPECT_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &restored, *info, diffBuf.data(), diffBuf.size() ) );
        SW_EXPECT_EQUAL( 999, restored._hp );
    }

    // Alias 해시로도 diff 적용
    struct DiffAliasActor
    {
        int32 _currentHp{ 0 };
    };
    sw::TypeInfo aliasInfo;
    aliasInfo._name               = sw::hashed_string( "DiffAliasActor" );
    aliasInfo._fullyQualifiedName = sw::hashed_string( "sw::DiffAliasActor" );
    aliasInfo._size               = sizeof( DiffAliasActor );
    sw::PropertyInfo hpProp( sw::hashed_string( "_currentHp" ), sw::hashed_string( "int32" ),
                             SW_OFFSET_OF( DiffAliasActor, _currentHp ) );
    hpProp._listAlias.push_back( sw::hashed_string( "hp" ) );
    aliasInfo._listProperty.push_back( hpProp );

    DiffAliasActor cdo{};
    DiffAliasActor mod{};
    mod._currentHp = 42;
    sw::vector<uint8> aliasDiff;
    SW_EXPECT_TRUE( sw::ObjectDiffSerializer::serializeDiff( aliasDiff, &cdo, &mod, aliasInfo ) );

    // 직렬화 페이로드 상의 이름을 alias 해시로 위조해 matchesNameHash 경로를 검증합니다.
    if ( aliasDiff.size() >= sizeof( uint32 ) )
    {
        const uint32 aliasHash = sw::hashed_string( "hp" ).getHash();
        sw::Memory::copy( aliasDiff.data(), &aliasHash, sizeof( uint32 ) );
    }
    DiffAliasActor viaAlias{};
    SW_EXPECT_TRUE( sw::ObjectDiffSerializer::deserializeDiff( &viaAlias, aliasInfo, aliasDiff.data(), aliasDiff.size() ) );
    SW_EXPECT_EQUAL( 42, viaAlias._currentHp );

    {
        SW_TEST_DEFENSIVE_SCOPE( "Testing unknown property hash in ObjectDiff" );
        uint32            unknownHash = 0xDEADBEEFu;
        uint32            zeroPayload{ 0 };
        sw::vector<uint8> unknownDiff( sizeof( uint32 ) * 2 );
        sw::Memory::copy( unknownDiff.data(), &unknownHash, sizeof( uint32 ) );
        sw::Memory::copy( unknownDiff.data() + sizeof( uint32 ), &zeroPayload, sizeof( uint32 ) );
        SW_EXPECT_FALSE( sw::ObjectDiffSerializer::deserializeDiff( &viaAlias, aliasInfo, unknownDiff.data(), unknownDiff.size() ) );
    }
}

/**
 * @brief [ReflectionSerializationTest] 바이너리 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, BinaryRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id        = 999;
    src._title     = "BinaryTest";
    src._flags     = static_cast<int64>( sw::DummyBitFlag::OptionB );
    src._listScore = { 100, 200, 300 };

    sw::vector<uint8> buffer;
    sw::BinarySerializer::serialize( &src, *typeInfo, buffer );
    SW_EXPECT_TRUE( buffer.empty() == false );

    sw::ComplexData dst;
    bool            success = sw::BinarySerializer::deserialize( &dst, *typeInfo, buffer.data(), buffer.size() );

    SW_EXPECT_TRUE( success );
    SW_EXPECT_EQUAL( 999, dst._id );
    SW_EXPECT_EQUAL( sw::string( "BinaryTest" ), dst._title );
    SW_EXPECT_EQUAL( static_cast<int64>( sw::DummyBitFlag::OptionB ), dst._flags );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._listScore.size() ) );
    if ( dst._listScore.size() == 3 )
    {
        SW_EXPECT_EQUAL( 100, dst._listScore[0] );
        SW_EXPECT_EQUAL( 300, dst._listScore[2] );
    }
}

/**
 * @brief [ReflectionSerializationTest] 압축 바이너리(CompressedBinarySerializer) 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, CompressedBinaryRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id        = 888;
    src._title     = "CompressedBinaryTest";
    src._flags     = static_cast<int64>( sw::DummyBitFlag::OptionA ) | static_cast<int64>( sw::DummyBitFlag::OptionC );
    src._listScore = { 10, 20, 30, 40, 50 };

    // 1) RLE 압축 직렬화
    sw::vector<uint8> compBuffer;
    const bool        bSerializeOk = sw::BinarySerializer::serializeCompressed( &src, *typeInfo, compBuffer, sw::CompressionCodecType::RLE );
    SW_EXPECT_TRUE( bSerializeOk );
    SW_EXPECT_TRUE( compBuffer.empty() == false );

    // 2) 압축 해제 및 역직렬화
    sw::ComplexData dst;
    const bool      bDeserializeOk = sw::BinarySerializer::deserializeCompressed( &dst, *typeInfo, compBuffer.data(), compBuffer.size() );
    SW_EXPECT_TRUE( bDeserializeOk );
    SW_EXPECT_EQUAL( 888, dst._id );
    SW_EXPECT_EQUAL( sw::string( "CompressedBinaryTest" ), dst._title );
    SW_EXPECT_EQUAL( src._flags, dst._flags );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( dst._listScore.size() ) );
    if ( dst._listScore.size() == 5 )
    {
        SW_EXPECT_EQUAL( 10, dst._listScore[0] );
        SW_EXPECT_EQUAL( 50, dst._listScore[4] );
    }
}

/**
 * @brief [ReflectionSerializationTest] JSON 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, JsonRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id        = 777;
    src._title     = "JsonTest";
    src._listScore = { 5, 10, 15 };

    sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
    SW_EXPECT_TRUE( json.empty() == false );

    sw::ComplexData dst;
    bool            success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json );

    SW_EXPECT_TRUE( success );
    SW_EXPECT_EQUAL( 777, dst._id );
    SW_EXPECT_EQUAL( sw::string( "JsonTest" ), dst._title );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( dst._listScore.size() ) );
}

/**
 * @brief [ReflectionSerializationTest] XML 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, XmlRapidXmlRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id        = 888;
    src._title     = "XmlTest";
    src._listScore = { 1, 2, 3, 4 };

    sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
    SW_EXPECT_TRUE( xml.empty() == false );

    sw::ComplexData dst;
    bool            success = sw::XmlSerializer::deserialize( &dst, *typeInfo, xml );

    SW_EXPECT_TRUE( success );
    SW_EXPECT_EQUAL( 888, dst._id );
    SW_EXPECT_EQUAL( sw::string( "XmlTest" ), dst._title );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( dst._listScore.size() ) );
}

struct SimpleXmlBackend : public sw::IXmlBackend
{
    sw::string                                _result;
    sw::string                                _rootTagName;
    sw::vector<sw::string>                    _listOpenTag;
    sw::unordered_map<sw::string, sw::string> _mapKv;
    bool                                      _bOpenTagPending{ false };

    void closeOpenTag()
    {
        if ( _bOpenTagPending == false )
            return;
        _result += ">";
        _bOpenTagPending = false;
    }

    void beginNamedElement( const utf8* pTag )
    {
        closeOpenTag();
        sw::string tag( pTag != nullptr ? pTag : "" );
        _result += "<" + tag;
        _bOpenTagPending = true;
        _listOpenTag.push_back( tag );
    }

    void endNamedElement()
    {
        closeOpenTag();
        if ( _listOpenTag.empty() )
            return;
        _result += "</" + _listOpenTag.back() + ">";
        _listOpenTag.pop_back();
    }

    void initializeXmlSerialization( const utf8* pRootTag ) override
    {
        _rootTagName     = pRootTag != nullptr ? pRootTag : "";
        _result          = "<" + _rootTagName;
        _bOpenTagPending = true;
    }
    void writeValue( const utf8* pTag, const utf8* pValue ) override
    {
        closeOpenTag();
        _result += "<" + sw::string( pTag ) + ">" + sw::string( pValue ) + "</" + sw::string( pTag ) + ">";
        _mapKv[sw::string( pTag )] = pValue != nullptr ? pValue : "";
    }
    void writeAttribute( const utf8* pAttr, const utf8* pValue ) override
    {
        _result += " " + sw::string( pAttr ) + "=\"" + sw::string( pValue ) + "\"";
        _mapKv[sw::string( pAttr )] = pValue != nullptr ? pValue : "";
    }
    void beginArray( const utf8* pTag ) override
    {
        beginNamedElement( pTag );
    }
    void writeArrayItem( const utf8* pValue ) override
    {
        closeOpenTag();
        _result += "<item>" + sw::string( pValue ) + "</item>";
    }
    void endArray() override
    {
        endNamedElement();
    }
    void beginMap( const utf8* pTag ) override
    {
        beginNamedElement( pTag );
    }
    void beginMapEntry() override
    {
        beginNamedElement( "entry" );
    }
    void writeMapKey( const utf8* pKey ) override
    {
        closeOpenTag();
        _result += "<key>" + sw::string( pKey ) + "</key>";
    }
    void writeMapValue( const utf8* pValue ) override
    {
        closeOpenTag();
        _result += "<value>" + sw::string( pValue ) + "</value>";
    }
    void endMapEntry() override
    {
        endNamedElement();
    }
    void endMap() override
    {
        endNamedElement();
    }
    sw::string endSerialize() override
    {
        closeOpenTag();
        if ( _rootTagName.empty() == false )
            _result += "</" + _rootTagName + ">";
        return _result;
    }

    bool initializeXmlDeserialization( sw::string_view xmlStr, const utf8* pRootTag ) override
    {
        (void)pRootTag;
        sw::string str( xmlStr );
        size_t     pos{ 0 };
        while ( ( pos = str.find( '<', pos ) ) != sw::string::npos )
        {
            size_t closeTag = str.find( '>', pos );
            if ( closeTag == sw::string::npos )
                break;
            sw::string tag = str.substr( pos + 1, closeTag - pos - 1 );
            if ( tag.empty() == false && tag[0] != '/' )
            {
                if ( tag.back() == '/' )
                    tag.pop_back();
                while ( tag.empty() == false && tag.back() == ' ' )
                    tag.pop_back();

                size_t spacePos = tag.find( ' ' );
                if ( spacePos != sw::string::npos )
                {
                    sw::string attrs = tag.substr( spacePos + 1 );
                    tag              = tag.substr( 0, spacePos );
                    size_t attrPos{ 0 };
                    while ( attrPos < attrs.size() )
                    {
                        while ( attrPos < attrs.size() && attrs[attrPos] == ' ' )
                            ++attrPos;
                        size_t eqPos = attrs.find( '=', attrPos );
                        if ( eqPos == sw::string::npos )
                            break;
                        sw::string attrName = attrs.substr( attrPos, eqPos - attrPos );
                        size_t     valBegin = eqPos + 1;
                        if ( valBegin < attrs.size() && attrs[valBegin] == '"' )
                        {
                            ++valBegin;
                            size_t valEnd = attrs.find( '"', valBegin );
                            if ( valEnd == sw::string::npos )
                                break;
                            _mapKv[attrName] = attrs.substr( valBegin, valEnd - valBegin );
                            attrPos          = valEnd + 1;
                        }
                        else
                            break;
                    }
                }

                size_t endTagPos = str.find( "</" + tag + ">", closeTag );
                if ( endTagPos != sw::string::npos )
                {
                    sw::string val = str.substr( closeTag + 1, endTagPos - closeTag - 1 );
                    _mapKv[tag]    = val;
                }
            }
            pos = closeTag + 1;
        }
        return true;
    }
    bool readValue( const utf8* pTag, sw::string& outValue ) override
    {
        auto it = _mapKv.find( sw::string( pTag ) );
        if ( it != _mapKv.end() )
        {
            outValue = it->second;
            return true;
        }
        return false;
    }
    bool readAttribute( const utf8* pAttr, sw::string& outValue ) override
    {
        return readValue( pAttr, outValue );
    }
    bool iterateArray( const utf8*, const sw::XmlArrayItemDelegate& ) override
    {
        return false;
    }
    bool iterateMap( const utf8*, const sw::XmlMapItemDelegate& ) override
    {
        return false;
    }
};

/**
 * @brief [ReflectionSerializationTest] XML 어트리뷰트 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, XmlAttributeRoundtrip )
{
    sw::XmlDocumentBackend backend;
    backend.initializeXmlSerialization( "AttrRoot" );
    backend.writeAttribute( "_id", "42" );
    backend.writeAttribute( "_title", "AttrTitle" );
    backend.writeValue( "_note", "child-element" );
    const sw::string xml = backend.endSerialize();
    SW_EXPECT_TRUE( xml.find( "_id=\"42\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_title=\"AttrTitle\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "<_note>" ) != sw::string::npos );

    sw::XmlDocumentBackend reader;
    SW_EXPECT_TRUE( reader.initializeXmlDeserialization( xml.c_str(), "AttrRoot" ) );
    sw::string id, title, note;
    SW_EXPECT_TRUE( reader.readAttribute( "_id", id ) );
    SW_EXPECT_TRUE( reader.readAttribute( "_title", title ) );
    SW_EXPECT_TRUE( reader.readValue( "_note", note ) );
    SW_EXPECT_EQUAL( sw::string( "42" ), id );
    SW_EXPECT_EQUAL( sw::string( "AttrTitle" ), title );
    SW_EXPECT_EQUAL( sw::string( "child-element" ), note );
    SW_EXPECT_TRUE( reader.readValueOrAttribute( "_id", id ) );
}

/**
 * @brief [ReflectionSerializationTest] XML/JSON 키 대소문자 무시, 값은 유지
 */
SW_TEST_CASE( ReflectionSerializationTest, XmlJsonKeysIgnoreCaseValuesPreserveCase )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    // 프로퍼티 키/태그는 대소문자가 달라도 되고, 문자열 값은 대소문자를 유지해야 한다.
    const utf8* json =
        R"({"_ID":77,"_TITLE":"CaseSensitiveValue","_listScore":[1,2]})";
    sw::ComplexData fromJson;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &fromJson, *typeInfo, json ) );
    SW_EXPECT_EQUAL( 77, fromJson._id );
    SW_EXPECT_EQUAL( sw::string( "CaseSensitiveValue" ), fromJson._title );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( fromJson._listScore.size() ) );

    const utf8* xml =
        R"(<sw__ComplexData _ID="88" _TITLE="XmlCaseValue"><_listScore><item>3</item><ITEM>4</ITEM></_listScore></sw__ComplexData>)";
    sw::ComplexData fromXml;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &fromXml, *typeInfo, xml ) );
    SW_EXPECT_EQUAL( 88, fromXml._id );
    SW_EXPECT_EQUAL( sw::string( "XmlCaseValue" ), fromXml._title );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( fromXml._listScore.size() ) );
    SW_EXPECT_EQUAL( 3, fromXml._listScore[0] );
    SW_EXPECT_EQUAL( 4, fromXml._listScore[1] );

    SW_EXPECT_EQUAL( sw::string( "CaseSensitiveValue" ),
                     sw::JsonSerializer::extractStringField( json, "_title" ) );
    SW_EXPECT_TRUE( sw::JsonSerializer::extractStringField( json, "_title", false ).empty() );

    // 옵트아웃: 대소문자 구분 키 조회는 다른 대소문자를 바인딩하면 안 된다.
    sw::SerializeContext strictCtx = sw::SerializeContext::getDefault();
    strictCtx.setIgnoreCaseKeys( false );

    sw::ComplexData strictJson;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &strictJson, *typeInfo, json, strictCtx ) );
    SW_EXPECT_EQUAL( 101, strictJson._id ); // ComplexData 기본값, 77 아님
    SW_EXPECT_EQUAL( sw::string( "HeroData" ), strictJson._title );

    sw::ComplexData strictXml;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &strictXml, *typeInfo, xml, strictCtx ) );
    SW_EXPECT_EQUAL( 101, strictXml._id );
    SW_EXPECT_EQUAL( sw::string( "HeroData" ), strictXml._title );
    // 시퀀스 원소는 이름으로 조회하지 않고 순서대로 읽는다(원소 태그는 타입에 따라 달라짐).
    // 따라서 대소문자 정책과 무관하게 자식이 모두 들어온다. strict 는 프로퍼티/속성 이름 조회에만 적용된다.
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( strictXml._listScore.size() ) );
    SW_EXPECT_EQUAL( 3, strictXml._listScore[0] );
    SW_EXPECT_EQUAL( 4, strictXml._listScore[1] );
}

/**
 * @brief [ReflectionSerializationTest] 폭이 좁은(uint8) enum 을 역직렬화해도 인접 필드를 덮어쓰지 않는다.
 * @details EnumInfo::_size 가 실제 크기로 채워져야 writeValueToMemory 가 딱 그만큼만 쓴다.
 *          codegen 이 _size 를 안 넣으면 4바이트를 써서 뒤따르는 1바이트 필드들이 깨진다.
 */
SW_TEST_CASE( ReflectionSerializationTest, NarrowEnumDeserializeKeepsAdjacentBytes )
{
    const sw::EnumInfo* pEnumInfo =
        sw::engine::getTypeRegistry().findEnum( sw::hashed_string( "sw::NarrowEnum" ) );
    SW_ASSERT_TRUE( pEnumInfo != nullptr );
    SW_EXPECT_EQUAL( static_cast<uint32>( sizeof( sw::NarrowEnum ) ), static_cast<uint32>( pEnumInfo->_size ) );

    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NarrowEnumHost" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    sw::NarrowEnumHost host;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &host, *typeInfo, R"({"_mode":"Two"})" ) );
    SW_EXPECT_TRUE( host._mode == sw::NarrowEnum::Two );
    // enum 뒤 1바이트 필드들이 초기값 그대로여야 한다.
    SW_EXPECT_EQUAL( static_cast<uint32>( 0xAB ), static_cast<uint32>( host._guard ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0xCD ), static_cast<uint32>( host._guard2 ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0xEF ), static_cast<uint32>( host._guard3 ) );

    // 숫자 표기도 동일하게 동작한다.
    sw::NarrowEnumHost numeric;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &numeric, *typeInfo, R"({"_mode":1})" ) );
    SW_EXPECT_TRUE( numeric._mode == sw::NarrowEnum::One );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0xAB ), static_cast<uint32>( numeric._guard ) );

    // 왕복도 값이 유지된다.
    sw::NarrowEnumHost src;
    src._mode               = sw::NarrowEnum::Two;
    const sw::string   json = sw::JsonSerializer::serialize( &src, *typeInfo );
    sw::NarrowEnumHost dst;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &dst, *typeInfo, json ) );
    SW_EXPECT_TRUE( dst._mode == sw::NarrowEnum::Two );
    SW_EXPECT_EQUAL( static_cast<uint32>( 0xAB ), static_cast<uint32>( dst._guard ) );
}

/**
 * @brief [ReflectionSerializationTest] JSON 맵 컨테이너를 평범한 오브젝트 표현으로도 읽고 쓴다.
 */
SW_TEST_CASE( ReflectionSerializationTest, JsonMapUsesPlainObject )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    // 읽기: {"key":value,...}
    sw::ComplexData plain;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &plain, *typeInfo, R"({"_mapStat":{"atk":7,"def":3}})" ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( plain._mapStat.size() ) );
    SW_EXPECT_EQUAL( 7, plain._mapStat["atk"] );
    SW_EXPECT_EQUAL( 3, plain._mapStat["def"] );

    // 쓰기도 같은 표현이어야 한다(래핑 키가 나오면 안 됨).
    const sw::string json = sw::JsonSerializer::serialize( &plain, *typeInfo );
    SW_EXPECT_TRUE( json.find( "\"_mapStat\":{" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"map\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"entry\"" ) == sw::string::npos );

    // 레거시 래핑 형식({"map":[{"_name":..,"entry":..}]})은 더 이상 지원하지 않는다.
    // 알 수 없는 키 "map" 으로 취급되어 실패해야 한다.
    sw::ComplexData legacy;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize(
        &legacy, *typeInfo, R"({"map":[{"_name":"_mapStat","entry":{"hp":9}}]})" ) );
}

/**
 * @brief [ReflectionSerializationTest] 임의로 중첩된 컨테이너가 세 포맷 모두에서 왕복한다.
 * @details vector<vector<T>>, map<K,vector<T>>, map<K,map<K,V>>, vector<Struct>, map<K,Struct>.
 *          표현 형태에 의존하지 않는 안전망이라 직렬화 shape 을 바꿔도 그대로 유효하다.
 */
SW_TEST_CASE( ReflectionSerializationTest, NestedContainerRoundtripAllFormats )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NestedContainerActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    sw::NestedContainerActor src;
    src._grid = {
        { 1, 2 },
        { 3, 4, 5 }
    };
    src._namedRows["a"] = { 1.0f, 2.5f };
    src._namedRows["b"] = { -3.25f };
    // 임시 맵을 통째로 대입하면 DataRaceDetector 가 오탐하므로 직접 채운다.
    src._nestedMap["out"]["x"] = 7;
    src._nestedMap["out"]["y"] = 8;
    src._nestedMap["in"]["z"]  = 9;
    src._listInner.push_back( sw::NestedInner{ 11 } );
    src._listInner.push_back( sw::NestedInner{ 22 } );
    src._mapInner["m"]._x = 33;
    src._inner._x         = 42;

    const auto verify = [&]( const sw::NestedContainerActor& dst, const utf8* pLabel )
    {
        // 실패 시 어느 포맷인지 알 수 있게 라벨을 먼저 남긴다.
        std::fprintf( stdout, "[nested roundtrip] format=%s\n", pLabel );
        SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dst._grid.size() );
        SW_EXPECT_EQUAL( 5, dst._grid[1][2] );
        SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dst._namedRows.size() );
        SW_EXPECT_NEAR_EQUAL( -3.25f, dst._namedRows.at( "b" )[0], 1e-4f );
        SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dst._nestedMap.size() );
        SW_EXPECT_EQUAL( 8, dst._nestedMap.at( "out" ).at( "y" ) );
        SW_EXPECT_EQUAL( 9, dst._nestedMap.at( "in" ).at( "z" ) );
        SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dst._listInner.size() );
        SW_EXPECT_EQUAL( 22, dst._listInner[1]._x );
        SW_EXPECT_EQUAL( 33, dst._mapInner.at( "m" )._x );
        SW_EXPECT_EQUAL( 42, dst._inner._x );
    };

    {
        const sw::string         json = sw::JsonSerializer::serialize( &src, *typeInfo );
        sw::NestedContainerActor dst;
        SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &dst, *typeInfo, json ) );
        verify( dst, "json" );
    }
    {
        sw::vector<uint8> bin;
        sw::BinarySerializer::serialize( &src, *typeInfo, bin );
        sw::NestedContainerActor dst;
        SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &dst, *typeInfo, bin.data(), bin.size() ) );
        verify( dst, "binary" );
    }
    {
        const sw::string         xml = sw::XmlSerializer::serialize( &src, *typeInfo );
        sw::NestedContainerActor dst;
        SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &dst, *typeInfo, xml ) );
        verify( dst, "xml" );
    }
}

/**
 * @brief [ReflectionSerializationTest] JSON 시퀀스 컨테이너를 평범한 배열 표현으로도 읽는다(손으로 쓴 Config 등).
 */
SW_TEST_CASE( ReflectionSerializationTest, JsonSequenceAcceptsPlainArray )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    // 자연스러운 형식: "_listScore": [10, 20, 30]
    sw::ComplexData plain;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &plain, *typeInfo, R"({"_id":9,"_listScore":[10,20,30]})" ) );
    SW_EXPECT_EQUAL( 9, plain._id );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( plain._listScore.size() ) );
    SW_EXPECT_EQUAL( 10, plain._listScore[0] );
    SW_EXPECT_EQUAL( 30, plain._listScore[2] );

    // 빈 배열도 유효하다.
    sw::ComplexData empty;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &empty, *typeInfo, R"({"_listScore":[]})" ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( empty._listScore.size() ) );

    // 잘못된 원소 타입은 여전히 실패한다.
    sw::ComplexData bad;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &bad, *typeInfo, R"({"_listScore":[1,"nope"]})" ) );
}

/**
 * @brief [ReflectionSerializationTest] JSON/XML 엄격 역직렬화가 잘못된 컨테이너·필드 coerce 에서 실패
 */
SW_TEST_CASE( ReflectionSerializationTest, StrictDeserializeFailsOnBadContainerAndField )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing strict deserialization failure on bad containers and fields" );
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    sw::ComplexData jsonContainer;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonContainer, *typeInfo, R"({"_listScore":[1,"not_an_int"]})" ) );

    sw::ComplexData jsonField;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonField, *typeInfo, R"({"_id":"not_an_int"})" ) );

    sw::ComplexData xmlField;
    SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
        &xmlField, *typeInfo, R"(<sw__ComplexData _id="not_an_int"/>)" ) );

    sw::ComplexData xmlContainer;
    SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
        &xmlContainer, *typeInfo,
        R"(<sw__ComplexData><_listScore><item>1</item><item>not_an_int</item></_listScore></sw__ComplexData>)" ) );

    sw::ComplexData jsonOk;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonOk, *typeInfo, R"({"_id":7,"_listScore":[1,2]})" ) );
    SW_EXPECT_EQUAL( 7, jsonOk._id );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( jsonOk._listScore.size() ) );

    sw::ComplexData jsonMalformed;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonMalformed, *typeInfo, R"({"not_a_pair","_id":1})" ) );

    sw::ComplexData jsonUnknown;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserialize( &jsonUnknown, *typeInfo, R"({"_id":1,"NotARealField":2})" ) );

    sw::ComplexData xmlUnknown;
    SW_EXPECT_FALSE( sw::XmlSerializer::deserialize(
        &xmlUnknown, *typeInfo,
        R"(<sw__ComplexData _id="1" NotARealField="x"/>)" ) );

    const utf8* xmlUnknownStr =
        R"(<sw__ComplexData _id="1" NotARealField="x"/>)";
    sw::XmlDocumentBackend xmlBackend;
    sw::ComplexData        xmlBackendUnknown;
    SW_EXPECT_FALSE( sw::XmlSerializer::deserialize( &xmlBackendUnknown, *typeInfo, xmlBackend, xmlUnknownStr ) );

    sw::ComplexData binSrc;
    binSrc._id = 3;
    sw::vector<uint8> binBuf;
    sw::BinarySerializer::serialize( &binSrc, *typeInfo, binBuf );
    SW_ASSERT_TRUE( binBuf.size() >= sizeof( uint32 ) );
    uint32 propCount{ 0 };
    sw::Memory::copy( &propCount, binBuf.data(), sizeof( uint32 ) );
    propCount += 1;
    sw::Memory::copy( binBuf.data(), &propCount, sizeof( uint32 ) );
    const uint32 unknownTag  = 0xDEADBEEFu;
    const uint32 unknownWire = 0;
    const uint32 unknownSize = 0;
    const uint8* tagBytes    = reinterpret_cast<const uint8*>( &unknownTag );
    const uint8* wireBytes   = reinterpret_cast<const uint8*>( &unknownWire );
    const uint8* sizeBytes   = reinterpret_cast<const uint8*>( &unknownSize );
    binBuf.insert( binBuf.end(), tagBytes, tagBytes + sizeof( uint32 ) );
    binBuf.insert( binBuf.end(), wireBytes, wireBytes + sizeof( uint32 ) );
    binBuf.insert( binBuf.end(), sizeBytes, sizeBytes + sizeof( uint32 ) );
    sw::ComplexData binUnknown;
    SW_EXPECT_FALSE( sw::BinarySerializer::deserialize( &binUnknown, *typeInfo, binBuf.data(), binBuf.size() ) );
    sw::vector<sw::SchemaOrphanValue> binOrphans;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &binUnknown, *typeInfo, binBuf.data(), binBuf.size(),
                                                           &binOrphans ) );
    SW_EXPECT_TRUE( binOrphans.empty() == false );
}

/**
 * @brief [ReflectionSerializationTest] 커스텀 XML 백엔드
 */
SW_TEST_CASE( ReflectionSerializationTest, CustomXmlBackend )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id    = 999;
    src._title = "CustomBackend";

    SimpleXmlBackend backend;
    sw::string       xml = sw::XmlSerializer::serialize( &src, *typeInfo, backend );
    SW_EXPECT_TRUE( xml.empty() == false );

    sw::ComplexData  dst;
    SimpleXmlBackend readBackend;
    bool             success = sw::XmlSerializer::deserialize( &dst, *typeInfo, readBackend, xml );
    SW_EXPECT_TRUE( success );
    SW_EXPECT_EQUAL( 999, dst._id );
    SW_EXPECT_EQUAL( sw::string( "CustomBackend" ), dst._title );
}

/**
 * @brief [ReflectionSerializationTest] 커스텀 SerializeContext
 */
SW_TEST_CASE( ReflectionSerializationTest, CustomSerializeContext )
{
    sw::SerializeContext customCtx = sw::SerializeContext::getDefault();

    customCtx.registerTextHandler(
        sw::hashed_string( "int32" ),
        []( const void* pPtr )
    { return sw::to_string( ( *static_cast<const int32*>( pPtr ) ) * 10 ); },
        []( void* pPtr, std::string_view s )
    {
        int32 val{ 0 };
        sw::StringUtil::parseInt( s, val );
        *static_cast<int32*>( pPtr ) = val / 10;
        return true;
    } );

    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::ComplexData" ) );
    SW_EXPECT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    sw::ComplexData src;
    src._id = 50;

    sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo, customCtx );
    SW_EXPECT_TRUE( json.find( "\"_id\":500" ) != sw::string::npos );

    sw::ComplexData dst;
    bool            success = sw::JsonSerializer::deserialize( &dst, *typeInfo, json, customCtx );
    SW_EXPECT_TRUE( success );
    SW_EXPECT_EQUAL( 50, dst._id );
}

/**
 * @brief [ReflectionSerializationTest] 누락 필드의 PROPERTY Default
 */
SW_TEST_CASE( ReflectionSerializationTest, PropertyDefaultOnMissing )
{
    struct DefaultActor
    {
        int32      _mana{ 0 };
        sw::string _title = "unset";
    };

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "DefaultActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::DefaultActor" );
    info._size               = sizeof( DefaultActor );

    sw::PropertyInfo manaProp( sw::hashed_string( "_mana" ), sw::hashed_string( "int32" ),
                               SW_OFFSET_OF( DefaultActor, _mana ) );
    manaProp._metadata._defaultValue = "75";
    sw::PropertyInfo titleProp( sw::hashed_string( "_title" ), sw::hashed_string( "string" ),
                                SW_OFFSET_OF( DefaultActor, _title ) );
    titleProp._metadata._defaultValue  = "Apprentice";
    titleProp._metadata._bXmlAttribute = SW_TRUE;
    info._listProperty                 = { manaProp, titleProp };

    DefaultActor actor;
    const utf8*  emptyXml = R"(<?xml version="1.0"?><DefaultActor></DefaultActor>)";
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &actor, info, emptyXml ) );
    SW_EXPECT_EQUAL( 75, actor._mana );
    SW_EXPECT_EQUAL( sw::string( "Apprentice" ), actor._title );

    DefaultActor jsonActor;
    jsonActor._mana  = 1;
    jsonActor._title = "x";
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &jsonActor, info, "{}" ) );
    SW_EXPECT_EQUAL( 75, jsonActor._mana );
    SW_EXPECT_EQUAL( sw::string( "Apprentice" ), jsonActor._title );

    // 에셋의 명시 값이 Default 메타데이터보다 우선한다.
    DefaultActor overridden;
    const utf8*  filledXml =
        R"(<?xml version="1.0"?><DefaultActor _title="Mage" _mana="10"/>)";
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &overridden, info, filledXml ) );
    SW_EXPECT_EQUAL( 10, overridden._mana );
    SW_EXPECT_EQUAL( sw::string( "Mage" ), overridden._title );
}

/**
 * @brief [ReflectionSerializationTest] 프로퍼티 Alias 와 재정렬
 */
SW_TEST_CASE( ReflectionSerializationTest, PropertyAliasAndReorderingTest )
{
    struct AliasTestActor
    {
        int32 _currentHp = 100;
    };

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "AliasTestActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::AliasTestActor" );
    info._size               = sizeof( AliasTestActor );
    info._listProperty       = {
        { sw::hashed_string( "_currentHp" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( AliasTestActor, _currentHp ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr, sw::hashed_string( "hp" ) }
    };

    sw::string     oldJson = "{\"hp\": 250}";
    AliasTestActor actor;
    bool           jsonOk = sw::JsonSerializer::deserialize( &actor, info, oldJson );
    SW_EXPECT_TRUE( jsonOk );
    SW_EXPECT_EQUAL( 250, actor._currentHp );

    AliasTestActor xmlActor;
    xmlActor._currentHp = 100;
    const utf8* oldXml  = R"(<?xml version="1.0"?><AliasTestActor hp="250"/>)";
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &xmlActor, info, oldXml ) );
    SW_EXPECT_EQUAL( 250, xmlActor._currentHp );

    // 복수 Alias (codegen AliasAndReorderTestActor: hp + HitPoints)
    const sw::TypeInfo* multiAliasInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AliasAndReorderTestActor" ) );
    SW_ASSERT_NOT_NULL( multiAliasInfo );
    sw::AliasAndReorderTestActor multi{};
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &multi, *multiAliasInfo, R"({"HitPoints":33,"_score":1})" ) );
    SW_EXPECT_EQUAL( 33, multi._currentHp );

    // PROPERTY(Default) 없는 누락 필드는 생성 시 값을 유지한다.
    AliasTestActor missingActor;
    missingActor._currentHp = 42;
    const utf8* emptyXml    = R"(<?xml version="1.0"?><AliasTestActor></AliasTestActor>)";
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &missingActor, info, emptyXml ) );
    SW_EXPECT_EQUAL( 42, missingActor._currentHp );

    struct ReorderActor1
    {
        int32 _fieldA = 10;
        int32 _fieldB = 20;
    };

    sw::TypeInfo info1;
    info1._name               = sw::hashed_string( "ReorderActor" );
    info1._fullyQualifiedName = sw::hashed_string( "sw::ReorderActor" );
    info1._size               = sizeof( ReorderActor1 );
    info1._listProperty       = {
        {sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( ReorderActor1, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
        {sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( ReorderActor1, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
    };

    ReorderActor1     src;
    sw::vector<uint8> binBuf;
    sw::BinarySerializer::serialize( &src, info1, binBuf );

    sw::TypeInfo infoReordered = info1;
    std::swap( infoReordered._listProperty[0], infoReordered._listProperty[1] );

    ReorderActor1 dst;
    dst._fieldA = 0;
    dst._fieldB = 0;
    bool binOk  = sw::BinarySerializer::deserialize( &dst, infoReordered, binBuf.data(), binBuf.size() );
    SW_EXPECT_TRUE( binOk );
    SW_EXPECT_EQUAL( 10, dst._fieldA );
    SW_EXPECT_EQUAL( 20, dst._fieldB );
}

/**
 * @brief [ReflectionSerializationTest] 타입 Alias + 옛 XML 루트 태그로 로드
 */
SW_TEST_CASE( ReflectionSerializationTest, TypeAliasXmlLoad )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::LegacyRenameActor" ) );
    SW_ASSERT_NOT_NULL( typeInfo );

    sw::RenameCompatActor actor;
    actor._hp          = 1;
    const utf8* oldXml = R"(<?xml version="1.0"?><LegacyRenameActor _hp="77"/>)";
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &actor, *typeInfo, oldXml ) );
    SW_EXPECT_EQUAL( 77, actor._hp );
}

/**
 * @brief [ReflectionSerializationTest] 필드 추가/삭제/개명 레이아웃 진화
 */
SW_TEST_CASE( ReflectionSerializationTest, LayoutEvolveAddRemoveRename )
{
    struct LayoutV1
    {
        int32 _hp{ 0 };
        int32 _score{ 0 };
    };

    struct LayoutV2
    {
        int32 _hp{ 0 };
        int32 _mana{ 0 }; ///< V1에 없음 — Default 적용
    };

    sw::TypeInfo infoV1;
    infoV1._name               = sw::hashed_string( "LayoutActor" );
    infoV1._fullyQualifiedName = sw::hashed_string( "sw::LayoutActor" );
    infoV1._size               = sizeof( LayoutV1 );
    infoV1._listProperty       = {
        {   sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV1,    _hp )},
        {sw::hashed_string( "_score" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV1, _score )},
    };

    sw::PropertyInfo hpV2( sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( LayoutV2, _hp ),
                           false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr,
                           sw::hashed_string( "health" ) ); ///< 개명: 옛 키 health
    sw::PropertyInfo manaV2( sw::hashed_string( "_mana" ), sw::hashed_string( "int32" ),
                             SW_OFFSET_OF( LayoutV2, _mana ) );
    manaV2._metadata._defaultValue = "9";

    sw::TypeInfo infoV2;
    infoV2._name               = sw::hashed_string( "LayoutActor" );
    infoV2._fullyQualifiedName = sw::hashed_string( "sw::LayoutActor" );
    infoV2._size               = sizeof( LayoutV2 );
    infoV2._listProperty       = { hpV2, manaV2 };

    // --- Binary: V1 blob → V2 (score 스킵, mana Default, hp 매칭) ---
    LayoutV1          v1{ 40, 99 };
    sw::vector<uint8> bin;
    sw::BinarySerializer::serialize( &v1, infoV1, bin );

    LayoutV2 fromBin{};
    fromBin._hp   = -1;
    fromBin._mana = -1;
    sw::vector<sw::SchemaOrphanValue> binOrphans;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeSoft( &fromBin, infoV2, bin.data(), bin.size(), &binOrphans ) );
    SW_EXPECT_EQUAL( 40, fromBin._hp );
    SW_EXPECT_EQUAL( 9, fromBin._mana );
    SW_EXPECT_TRUE( binOrphans.empty() == false );

    // --- XML: 옛 필드명 health + 제거된 score + 신규 mana 누락 ---
    LayoutV2 fromXml{};
    fromXml._hp   = -1;
    fromXml._mana = -1;
    const utf8* oldXml =
        R"(<?xml version="1.0"?><LayoutActor health="55" _score="1"/>)";
    sw::vector<sw::SchemaOrphanValue> xmlOrphans;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserializeSoft( &fromXml, infoV2, oldXml, &xmlOrphans ) );
    SW_EXPECT_EQUAL( 55, fromXml._hp );
    SW_EXPECT_EQUAL( 9, fromXml._mana );
    SW_EXPECT_TRUE( xmlOrphans.empty() == false );

    // --- JSON: 동일 ---
    LayoutV2 fromJson{};
    fromJson._hp   = -1;
    fromJson._mana = -1;
    sw::vector<sw::SchemaOrphanValue> jsonOrphans;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserializeSoft( &fromJson, infoV2, R"({"health":66,"_score":2})", &jsonOrphans ) );
    SW_EXPECT_EQUAL( 66, fromJson._hp );
    SW_EXPECT_EQUAL( 9, fromJson._mana );
    SW_EXPECT_TRUE( jsonOrphans.empty() == false );

    // --- 필드 타입명 별칭(int32 → int32)으로 텍스트 파싱 ---
    struct IntAliasHolder
    {
        int32 _v{ 0 };
    };
    sw::TypeInfo intAliasInfo;
    intAliasInfo._name               = sw::hashed_string( "IntAliasHolder" );
    intAliasInfo._fullyQualifiedName = sw::hashed_string( "sw::IntAliasHolder" );
    intAliasInfo._size               = sizeof( IntAliasHolder );
    intAliasInfo._listProperty       = {
        { sw::hashed_string( "_v" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( IntAliasHolder, _v ) }
    };
    IntAliasHolder holder{};
    SW_EXPECT_TRUE( sw::XmlSerializer::deserialize(
        &holder, intAliasInfo, R"(<?xml version="1.0"?><IntAliasHolder _v="123"/>)" ) );
    SW_EXPECT_EQUAL( 123, holder._v );
}

/**
 * @brief [ReflectionSerializationTest] 바이너리 버전 헤더
 */
SW_TEST_CASE( ReflectionSerializationTest, BinaryVersionHeaderTest )
{
    struct VersionedActor
    {
        int32 _fieldA = 777;
        int32 _fieldB = 888;
    };

    VersionedActor actor;

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "VersionedActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::VersionedActor" );
    info._size               = sizeof( VersionedActor );
    info._listProperty       = {
        {sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( VersionedActor, _fieldA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
        {sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( VersionedActor, _fieldB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
    };

    sw::vector<uint8> buffer;
    sw::BinarySerializer::serializeVersioned( 102, &actor, info, buffer );
    SW_EXPECT_TRUE( buffer.size() > sizeof( uint32 ) );

    VersionedActor restored;
    restored._fieldA = 0;
    restored._fieldB = 0;
    uint32 readVersion{ 0 };
    bool   ok = sw::BinarySerializer::deserializeVersioned( readVersion, &restored, info, buffer.data(), buffer.size(),
                                                            102u );
    SW_EXPECT_TRUE( ok );
    SW_EXPECT_EQUAL( 102u, readVersion );
    SW_EXPECT_EQUAL( 777, restored._fieldA );
    SW_EXPECT_EQUAL( 888, restored._fieldB );

    // fromVersion != currentVersion → migrate 콜백
    static bool s_migrateCalled{ false };
    s_migrateCalled = false;
    auto migrateFn  = []( const sw::SchemaMigrateContext& ctx ) -> bool
    {
        s_migrateCalled = true;
        SW_EXPECT_EQUAL( 102u, ctx._fromVersion );
        SW_EXPECT_EQUAL( 103u, ctx._toVersion );
        static_cast<VersionedActor*>( ctx._pInstance )->_fieldA += 1;
        return true;
    };
    restored._fieldA = 0;
    restored._fieldB = 0;
    readVersion      = 0;
    ok               = sw::BinarySerializer::deserializeVersioned( readVersion, &restored, info, buffer.data(), buffer.size(), 103u,
                                                                   +migrateFn );
    SW_EXPECT_TRUE( ok );
    SW_EXPECT_TRUE( s_migrateCalled );
    SW_EXPECT_EQUAL( 778, restored._fieldA );
}

/**
 * @brief [ReflectionSerializationTest] 필드 타입 변경 (int32→string) binary coerce + Json/Xml versioned
 */
SW_TEST_CASE( ReflectionSerializationTest, FieldTypeChangeAndTextVersioned )
{
    struct IntHp
    {
        int32 _hp{ 0 };
    };
    struct StrHp
    {
        sw::string _hp;
    };

    sw::TypeInfo intInfo;
    intInfo._name               = sw::hashed_string( "IntHp" );
    intInfo._fullyQualifiedName = sw::hashed_string( "sw::IntHp" );
    intInfo._size               = sizeof( IntHp );
    intInfo._listProperty       = {
        { sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( IntHp, _hp ) }
    };

    sw::TypeInfo strInfo;
    strInfo._name               = sw::hashed_string( "StrHp" );
    strInfo._fullyQualifiedName = sw::hashed_string( "sw::StrHp" );
    strInfo._size               = sizeof( StrHp );
    strInfo._listProperty       = {
        { sw::hashed_string( "_hp" ), sw::hashed_string( "string" ), SW_OFFSET_OF( StrHp, _hp ) }
    };

    IntHp             src{ 42 };
    sw::vector<uint8> bin;
    sw::BinarySerializer::serializeVersioned( 1, &src, intInfo, bin );

    StrHp  dst;
    uint32 ver{ 0 };
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &dst, strInfo, bin.data(), bin.size(), 1u ) );
    SW_EXPECT_EQUAL( 1u, ver );
    SW_EXPECT_TRUE( dst._hp == "42" );

    // string → int32 (quoted JSON)
    StrHp      strSrc{ "99" };
    sw::string json = sw::JsonSerializer::serializeVersioned( 3, &strSrc, strInfo );
    SW_EXPECT_TRUE( json.find( "\"_schemaVersion\":3" ) != sw::string::npos );

    IntHp fromJson{};
    ver = 0;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &fromJson, intInfo, json, 3u ) );
    SW_EXPECT_EQUAL( 3u, ver );
    SW_EXPECT_EQUAL( 99, fromJson._hp );

    // Xml versioned + int32→string coerce
    sw::string xml = sw::XmlSerializer::serializeVersioned( 4, &src, intInfo );
    SW_EXPECT_TRUE( xml.find( "_schemaVersion" ) != sw::string::npos );
    StrHp fromXml;
    ver = 0;
    SW_EXPECT_TRUE( sw::XmlSerializer::deserializeVersioned( ver, &fromXml, strInfo, xml, 4u ) );
    SW_EXPECT_EQUAL( 4u, ver );
    SW_EXPECT_TRUE( fromXml._hp == "42" );
}

/**
 * @brief [ReflectionSerializationTest] float 프로퍼티를 string 으로 바꿔도 **숫자가** 살아남는다
 * @details POD -> string 이관은 payload **크기**로 타입을 짐작했다. 그런데 `sizeof(float32)` 는
 *          `sizeof(int32)` 와 같아서 int32 가지가 먼저 걸리고 float32 가지는 **영영 돌지 않았다** —
 *          `1.5f` 가 그 비트값인 `"1069547520"` 으로 적혔다. 형제 케이스
 *          `FieldTypeChangeAndTextVersioned`(int32 -> string)는 크기 짐작이 우연히 맞아서 초록이었다.
 *          전선 타입은 바이너리 태그가 이미 들고 있었고, 여기까지 넘겨 주지 않았을 뿐이다.
 */
SW_TEST_CASE( ReflectionSerializationTest, FloatFieldToStringCoerceKeepsTheNumber )
{
    struct FloatSpeed
    {
        float32 _speed{ 0.0f };
    };
    struct StrSpeed
    {
        sw::string _speed;
    };

    sw::TypeInfo floatInfo;
    floatInfo._name               = sw::hashed_string( "FloatSpeed" );
    floatInfo._fullyQualifiedName = sw::hashed_string( "sw::FloatSpeed" );
    floatInfo._size               = sizeof( FloatSpeed );
    floatInfo._listProperty       = {
        { sw::hashed_string( "_speed" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( FloatSpeed, _speed ) }
    };

    sw::TypeInfo strInfo;
    strInfo._name               = sw::hashed_string( "StrSpeed" );
    strInfo._fullyQualifiedName = sw::hashed_string( "sw::StrSpeed" );
    strInfo._size               = sizeof( StrSpeed );
    strInfo._listProperty       = {
        { sw::hashed_string( "_speed" ), sw::hashed_string( "string" ), SW_OFFSET_OF( StrSpeed, _speed ) }
    };

    FloatSpeed        src{ 1.5f };
    sw::vector<uint8> bin;
    sw::BinarySerializer::serializeVersioned( 1, &src, floatInfo, bin );

    StrSpeed dst;
    uint32   ver{ 0 };
    SW_ASSERT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &dst, strInfo, bin.data(), bin.size(), 1u ) );
    SW_EXPECT_EQUAL( 1u, ver );

    // 적힌 글자 모양(`to_string` 의 자릿수)에 기대지 않고 **다시 읽어 숫자로** 본다.
    float32 roundTripped{ 0.0f };
    SW_EXPECT_TRUE_MSG( sw::StringUtil::parseFloat( dst._speed, roundTripped ),
                        "float 프로퍼티가 숫자가 아닌 글자로 이관됐습니다" );
    SW_EXPECT_NEAR_EQUAL( 1.5f, roundTripped, 0.0001f );

    // 큰 부호 없는 값도 비트값으로 접히지 않아야 한다.
    struct UintCount
    {
        uint32 _count{ 0 };
    };
    sw::TypeInfo uintInfo;
    uintInfo._name               = sw::hashed_string( "UintCount" );
    uintInfo._fullyQualifiedName = sw::hashed_string( "sw::UintCount" );
    uintInfo._size               = sizeof( UintCount );
    uintInfo._listProperty       = {
        { sw::hashed_string( "_count" ), sw::hashed_string( "uint32" ), SW_OFFSET_OF( UintCount, _count ) }
    };

    sw::TypeInfo strCountInfo;
    strCountInfo._name               = sw::hashed_string( "StrCount" );
    strCountInfo._fullyQualifiedName = sw::hashed_string( "sw::StrCount" );
    strCountInfo._size               = sizeof( StrSpeed );
    strCountInfo._listProperty       = {
        { sw::hashed_string( "_count" ), sw::hashed_string( "string" ), SW_OFFSET_OF( StrSpeed, _speed ) }
    };

    UintCount         uintSrc{ 4000000000u };
    sw::vector<uint8> uintBin;
    sw::BinarySerializer::serializeVersioned( 1, &uintSrc, uintInfo, uintBin );

    StrSpeed uintDst;
    ver = 0;
    SW_ASSERT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &uintDst, strCountInfo, uintBin.data(), uintBin.size(), 1u ) );
    SW_EXPECT_TRUE_MSG( uintDst._speed == "4000000000", "부호 없는 값이 int32 로 읽혀 음수가 됐습니다" );
}

/**
 * @brief [ReflectionSerializationTest] 스칼라 필드의 타입이 바뀌면 **값으로** 옮긴다(비트를 재해석하지 않는다)
 * @details 이관은 제 타입으로 끝까지 읽히는지부터 봤다. 크기가 같은 스칼라는 그 읽기가 늘 성공해서 int32 100 이
 *          float32 1.4e-43 이 됐고, 문자열은 int32 0 을 길이 0 으로 읽어 "" 가 됐다. 이제 기록 타입을 아는 스칼라는
 *          JSON · XML 처럼 기록 타입의 텍스트를 대상 타입으로 다시 읽는다. 텍스트가 맞지 않는 쌍(float32 1.5 → int32)은
 *          옮기지 않고 orphan 으로 남긴다. 소프트 읽기가 예전처럼 제 타입으로 다시 읽으면 1069547520 이 들어간다.
 */
SW_TEST_CASE( ReflectionSerializationTest, ScalarFieldTypeChangeMovesTheValue )
{
    struct IntValue
    {
        int32 _value{ 0 };
    };
    struct FloatValue
    {
        float32 _value{ 0.0f };
    };
    struct StrValue
    {
        sw::string _value;
    };

    sw::TypeInfo intInfo;
    intInfo._name               = sw::hashed_string( "ProbeIntValue" );
    intInfo._fullyQualifiedName = sw::hashed_string( "sw::ProbeIntValue" );
    intInfo._size               = sizeof( IntValue );
    intInfo._listProperty       = {
        { sw::hashed_string( "_value" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( IntValue, _value ) }
    };

    sw::TypeInfo floatInfo;
    floatInfo._name               = sw::hashed_string( "ProbeFloatValue" );
    floatInfo._fullyQualifiedName = sw::hashed_string( "sw::ProbeFloatValue" );
    floatInfo._size               = sizeof( FloatValue );
    floatInfo._listProperty       = {
        { sw::hashed_string( "_value" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( FloatValue, _value ) }
    };

    sw::TypeInfo strInfo;
    strInfo._name               = sw::hashed_string( "ProbeStrValue" );
    strInfo._fullyQualifiedName = sw::hashed_string( "sw::ProbeStrValue" );
    strInfo._size               = sizeof( StrValue );
    strInfo._listProperty       = {
        { sw::hashed_string( "_value" ), sw::hashed_string( "string" ), SW_OFFSET_OF( StrValue, _value ) }
    };

    // int32 100 → float32 100
    const IntValue    intSrc{ 100 };
    sw::vector<uint8> intBin;
    sw::BinarySerializer::serializeVersioned( 1, &intSrc, intInfo, intBin );
    FloatValue floatDst{};
    uint32     ver{ 0 };
    SW_ASSERT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &floatDst, floatInfo, intBin.data(), intBin.size(), 1u ) );
    SW_EXPECT_NEAR_EQUAL( 100.0f, floatDst._value, 0.0001f );

    // int32 0 → string "0"
    const IntValue    zeroSrc{ 0 };
    sw::vector<uint8> zeroBin;
    sw::BinarySerializer::serializeVersioned( 1, &zeroSrc, intInfo, zeroBin );
    StrValue strDst;
    ver = 0;
    SW_ASSERT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &strDst, strInfo, zeroBin.data(), zeroBin.size(), 1u ) );
    SW_EXPECT_TRUE_MSG( strDst._value == "0", "int32 0 이 길이 0 인 문자열로 읽혔습니다" );

    // float32 1.5 → int32 는 옮기지 않는다. migrate 가 orphan 을 받아 주면 읽기는 성공하지만 값은 그대로다.
    const FloatValue  floatSrc{ 1.5f };
    sw::vector<uint8> floatBin;
    sw::BinarySerializer::serializeVersioned( 1, &floatSrc, floatInfo, floatBin );
    IntValue intDst{ -1 };
    ver                = 0;
    auto acceptOrphans = []( const sw::SchemaMigrateContext& ) -> bool
    {
        return true;
    };
    SW_EXPECT_TRUE( sw::BinarySerializer::deserializeVersioned( ver, &intDst, intInfo, floatBin.data(), floatBin.size(), 1u, +acceptOrphans ) );
    SW_EXPECT_TRUE_MSG( intDst._value == -1, "float32 1.5 의 비트가 int32 로 재해석됐습니다" );
}

/**
 * @brief [ReflectionSerializationTest] 기록 타입이 다른 바이너리 orphan 을 적용해도 **이웃 필드를 덮지 않는다**
 * @details `applyOrphanTo` 는 기록 타입으로 프로퍼티 자리에 먼저 읽었다. int16 자리에 int32 를 읽으면 바로 뒤 필드의
 *          두 바이트까지 덮어썼고, string 자리였다면 객체를 부쉈다. `applyOrphanToPath` 는 힌트가 없으면 프로퍼티 타입을
 *          기록 타입으로 가정해 같은 일을 했다. 이제 둘 다 본 역직렬화 경로처럼 타입이 다르면 이관으로 옮긴다.
 */
SW_TEST_CASE( ReflectionSerializationTest, ApplyOrphanWithOtherWireTypeKeepsNeighbors )
{
    struct NarrowPair
    {
        int16 _small{ 0 };
        int16 _neighbor{ 99 };
    };
    struct TextHolder
    {
        sw::string _label;
    };

    sw::TypeInfo pairInfo;
    pairInfo._name               = sw::hashed_string( "ProbeNarrowPair" );
    pairInfo._fullyQualifiedName = sw::hashed_string( "sw::ProbeNarrowPair" );
    pairInfo._size               = sizeof( NarrowPair );
    pairInfo._listProperty       = {
        {   sw::hashed_string( "_small" ), sw::hashed_string( "int16" ), SW_OFFSET_OF( NarrowPair,    _small )},
        {sw::hashed_string( "_neighbor" ), sw::hashed_string( "int16" ), SW_OFFSET_OF( NarrowPair, _neighbor )}
    };

    sw::TypeInfo textInfo;
    textInfo._name               = sw::hashed_string( "ProbeTextHolder" );
    textInfo._fullyQualifiedName = sw::hashed_string( "sw::ProbeTextHolder" );
    textInfo._size               = sizeof( TextHolder );
    textInfo._listProperty       = {
        { sw::hashed_string( "_label" ), sw::hashed_string( "string" ), SW_OFFSET_OF( TextHolder, _label ) }
    };

    auto makeInt32Orphan = []( const utf8* pName, int32 value ) -> sw::SchemaOrphanValue
    {
        sw::SchemaOrphanValue orphan;
        orphan._name         = sw::hashed_string( pName );
        orphan._nameHash     = orphan._name.getHash();
        orphan._wireTypeHash = sw::hashed_string( "int32" ).getHash();
        orphan._listBinary.resize( sizeof( value ) );
        sw::Memory::copy( orphan._listBinary.data(), &value, sizeof( value ) );
        return orphan;
    };

    // int32 5 로 기록된 `_small` 을 int16 자리에 적용한다. 뒤의 `_neighbor` 는 그대로여야 한다.
    const sw::vector<sw::SchemaOrphanValue> listPairOrphan{ makeInt32Orphan( "_small", 5 ) };
    NarrowPair                              target;
    sw::SchemaMigrateContext                ctx;
    ctx._pInstance = &target;
    ctx._pTypeInfo = &pairInfo;
    ctx._pOrphans  = &listPairOrphan;
    const bool bTo = ctx.applyOrphanTo( sw::hashed_string( "_small" ) );
    SW_EXPECT_TRUE( bTo );
    SW_EXPECT_TRUE_MSG( target._small == 5, "int32 5 가 int16 으로 옮겨지지 않았습니다" );
    SW_EXPECT_TRUE_MSG( target._neighbor == 99, "applyOrphanTo 가 기록 타입(int32)으로 제자리에 써서 이웃 필드를 덮었습니다" );

    target           = NarrowPair{};
    const bool bPath = ctx.applyOrphanToPath( "_small" );
    SW_EXPECT_TRUE( bPath );
    SW_EXPECT_TRUE_MSG( target._small == 5, "점 경로 판이 int32 5 를 옮기지 않았습니다" );
    SW_EXPECT_TRUE_MSG( target._neighbor == 99, "applyOrphanToPath 가 프로퍼티 타입으로 가정해 제자리에 읽었습니다" );

    // string 자리. 힙에 사는 긴 문자열이어야 제자리 쓰기가 객체를 부순다(짧으면 SSO 라 티가 안 난다).
    const sw::vector<sw::SchemaOrphanValue> listTextOrphan{ makeInt32Orphan( "_label", 42 ) };
    TextHolder                              holder;
    holder._label  = "a label long enough to live on the heap";
    ctx._pInstance = &holder;
    ctx._pTypeInfo = &textInfo;
    ctx._pOrphans  = &listTextOrphan;
    SW_EXPECT_TRUE( ctx.applyOrphanTo( sw::hashed_string( "_label" ) ) );
    SW_EXPECT_TRUE_MSG( holder._label == "42", "int32 42 가 문자열 \"42\" 로 옮겨지지 않았습니다" );
}

/**
 * @brief [ReflectionSerializationTest] migrate 없이 스키마 버전이 다르면 실패
 */
SW_TEST_CASE( ReflectionSerializationTest, VersionedDeserializeFailsWithoutMigrate )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing schema version mismatch without migration callback" );
    struct VersionedActor
    {
        int32 _fieldA{ 0 };
    };
    sw::TypeInfo info;
    info._name               = sw::hashed_string( "VersionMismatchActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::VersionMismatchActor" );
    info._size               = sizeof( VersionedActor );
    info._listProperty       = {
        { sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( VersionedActor, _fieldA ) }
    };

    VersionedActor    actor{ 7 };
    sw::vector<uint8> bin;
    sw::BinarySerializer::serializeVersioned( 1, &actor, info, bin );

    VersionedActor restored{ 0 };
    uint32         ver{ 0 };
    SW_EXPECT_FALSE( sw::BinarySerializer::deserializeVersioned( ver, &restored, info, bin.data(), bin.size(), 2u ) );
    SW_EXPECT_EQUAL( 1u, ver );

    sw::string json = sw::JsonSerializer::serializeVersioned( 1, &actor, info );
    ver             = 0;
    SW_EXPECT_FALSE( sw::JsonSerializer::deserializeVersioned( ver, &restored, info, json, 2u ) );

    sw::string xml = sw::XmlSerializer::serializeVersioned( 1, &actor, info );
    ver            = 0;
    SW_EXPECT_FALSE( sw::XmlSerializer::deserializeVersioned( ver, &restored, info, xml, 2u ) );
}

/**
 * @brief [ReflectionSerializationTest] 버전은 같고 orphan 만 있을 때 — **Binary 는 거절하고 텍스트는 받는다**
 * @details 이것은 "이렇게 되어야 한다" 가 아니라 **지금 실제로 이렇다** 를 못박는 케이스다.
 *          세 포맷이 `deserializeVersioned` 의 같은 스무 줄을 각자 복사해 갖고 있었고, 그 중 이 판단
 *          한 줄만 서로 달랐다 — 나란히 놓고 보기 전에는 아무도 몰랐다. 절차를 하나로 합치면서
 *          그 차이에 `SchemaOrphanPolicy` 라는 이름을 붙여 호출부에 드러냈고, 값은 **그대로 두었다**:
 *          텍스트를 엄격하게 바꾸면 모르는 필드가 하나만 있어도 씬·프리팹·머티리얼이 통째로 로드에
 *          실패한다. 그 선택이 옳은지는 `docs/06_Backlog.md` 에 질문으로 남겼다.
 * @note 그러므로 이 케이스가 깨졌다면 **정책을 바꾼 것**이다. 바꾼 것이 의도라면 여기 기대값과
 *       백로그의 질문을 같이 고칠 것. 의도가 아니라면 방금 씬 로딩을 깨뜨린 것이다.
 */
SW_TEST_CASE( ReflectionSerializationTest, OrphanOnlyPolicyDiffersByFormat )
{
    SW_TEST_DEFENSIVE_SCOPE( "Pinning the per-format orphan-only policy" );
    struct WideActor
    {
        int32 _fieldA{ 0 };
        int32 _fieldB{ 0 };
    };
    struct NarrowActor
    {
        int32 _fieldA{ 0 };
    };

    sw::TypeInfo wide;
    wide._name               = sw::hashed_string( "ProbeWideActor" );
    wide._fullyQualifiedName = sw::hashed_string( "sw::ProbeWideActor" );
    wide._size               = sizeof( WideActor );
    wide._listProperty       = {
        {sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( WideActor, _fieldA )},
        {sw::hashed_string( "_fieldB" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( WideActor, _fieldB )}
    };

    sw::TypeInfo narrow;
    narrow._name               = sw::hashed_string( "ProbeNarrowActor" );
    narrow._fullyQualifiedName = sw::hashed_string( "sw::ProbeNarrowActor" );
    narrow._size               = sizeof( NarrowActor );
    narrow._listProperty       = {
        { sw::hashed_string( "_fieldA" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( NarrowActor, _fieldA ) }
    };

    WideActor   source{ 7, 9 };
    NarrowActor target{ 0 };
    uint32      ver{ 0 };

    sw::vector<uint8> bin;
    sw::BinarySerializer::serializeVersioned( 1, &source, wide, bin );
    const bool bBinary = sw::BinarySerializer::deserializeVersioned( ver, &target, narrow, bin.data(), bin.size(), 1u );

    const sw::string json = sw::JsonSerializer::serializeVersioned( 1, &source, wide );
    ver                   = 0;
    const bool bJson      = sw::JsonSerializer::deserializeVersioned( ver, &target, narrow, json, 1u );

    const sw::string xml = sw::XmlSerializer::serializeVersioned( 1, &source, wide );
    ver                  = 0;
    const bool bXml      = sw::XmlSerializer::deserializeVersioned( ver, &target, narrow, xml, 1u );

    SW_EXPECT_TRUE_MSG( bBinary == false,
                        "Binary 가 orphan 을 받아들였습니다 — SchemaOrphanPolicy::Reject 가 무력해졌습니다" );
    SW_EXPECT_TRUE_MSG( bJson,
                        "JSON 이 orphan 만으로 실패했습니다 — 모르는 필드 하나로 파일 전체가 안 읽힙니다" );
    SW_EXPECT_TRUE_MSG( bXml,
                        "XML 이 orphan 만으로 실패했습니다 — 모르는 필드 하나로 씬이 통째로 안 읽힙니다" );

    // 텍스트가 받아들였다면 **아는 필드는 제대로 들어왔어야** 한다. 그러지 않으면 "조용히 통과" 가
    // 아니라 "조용히 망가뜨림" 이다.
    SW_EXPECT_EQUAL( 7, target._fieldA );
}

/**
 * @brief [ReflectionSerializationTest] orphan 구조 이동 + PROPERTY Alias 개명
 */
SW_TEST_CASE( ReflectionSerializationTest, StructuralMoveAndPropertyAlias )
{
    struct NestedStats
    {
        int32 _hp{ 0 };
    };
    struct NestedActor
    {
        NestedStats _stats;
    };
    struct RenamedActor
    {
        int32 _hitPoints{ 0 };
    };

    sw::TypeInfo nestedStatsInfo;
    nestedStatsInfo._name               = sw::hashed_string( "NestedStats" );
    nestedStatsInfo._fullyQualifiedName = sw::hashed_string( "sw::NestedStats" );
    nestedStatsInfo._size               = sizeof( NestedStats );
    nestedStatsInfo._listProperty       = {
        { sw::hashed_string( "_hp" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( NestedStats, _hp ) }
    };
    sw::engine::getTypeRegistry().registerClass( nestedStatsInfo );

    sw::TypeInfo nestedActorInfo;
    nestedActorInfo._name               = sw::hashed_string( "NestedActor" );
    nestedActorInfo._fullyQualifiedName = sw::hashed_string( "sw::NestedActor" );
    nestedActorInfo._size               = sizeof( NestedActor );
    nestedActorInfo._listProperty       = {
        { sw::hashed_string( "_stats" ), sw::hashed_string( "sw::NestedStats" ), SW_OFFSET_OF( NestedActor, _stats ) }
    };

    // JSON orphan `_hp` → `_stats._hp`
    NestedActor nested{};
    uint32      ver{ 0 };
    auto        moveOrphan = []( const sw::SchemaMigrateContext& ctx ) -> bool
    {
        return ctx.applyOrphanToPath( "_stats._hp" );
    };
    SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &nested, nestedActorInfo, R"({"_schemaVersion":1,"_hp":77})",
                                                              2u, +moveOrphan ) );
    SW_EXPECT_EQUAL( 77, nested._stats._hp );

    // 필드 개명: Alias 로 옛 키 로드 (legacyTypeInfo 스테이징 불필요)
    sw::TypeInfo renamedInfo;
    renamedInfo._name               = sw::hashed_string( "RenamedActor" );
    renamedInfo._fullyQualifiedName = sw::hashed_string( "sw::RenamedActor" );
    renamedInfo._size               = sizeof( RenamedActor );
    sw::PropertyInfo hpProp( sw::hashed_string( "_hitPoints" ), sw::hashed_string( "int32" ),
                             SW_OFFSET_OF( RenamedActor, _hitPoints ) );
    hpProp._listAlias.push_back( sw::hashed_string( "_hp" ) );
    renamedInfo._listProperty.push_back( hpProp );

    RenamedActor renamed{};
    ver = 0;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserializeVersioned( ver, &renamed, renamedInfo,
                                                              R"({"_schemaVersion":1,"_hp":55})", 1u ) );
    SW_EXPECT_EQUAL( 55, renamed._hitPoints );
}

/**
 * @brief [ReflectionSerializationTest] JSON pretty print
 */
SW_TEST_CASE( ReflectionSerializationTest, JsonPrettyPrint )
{
    struct SimpleJsonActor
    {
        int32 _val = 42;
    } actor;

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "SimpleJsonActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::SimpleJsonActor" );
    info._size               = sizeof( SimpleJsonActor );
    info._listProperty       = {
        { sw::hashed_string( "_val" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( SimpleJsonActor, _val ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr }
    };

    sw::string prettyStr = sw::JsonSerializer::serializePretty( &actor, info, 4 );
    SW_EXPECT_TRUE( prettyStr.find( '\n' ) != sw::string::npos );
    SW_EXPECT_TRUE( prettyStr.find( "    \"_val\": 42" ) != sw::string::npos );
}

/**
 * @brief [ReflectionCloningTest] 오브젝트 딥카피
 */
SW_TEST_CASE( ReflectionCloningTest, ObjectDeepCopyClone )
{
    struct CloneableActor
    {
        int32   _health = 100;
        float32 _speed  = 5.5f;
    } srcActor, dstActor;

    srcActor._health = 250;
    srcActor._speed  = 12.0f;

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "CloneableActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::CloneableActor" );
    info._size               = sizeof( CloneableActor );
    info._listProperty       = {
        {sw::hashed_string( "_health" ),   sw::hashed_string( "int32" ), SW_OFFSET_OF( CloneableActor, _health ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
        { sw::hashed_string( "_speed" ), sw::hashed_string( "float32" ), SW_OFFSET_OF( CloneableActor,  _speed ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
    };

    bool cloneOk = sw::BinarySerializer::cloneObject( &dstActor, &srcActor, info );
    SW_EXPECT_TRUE( cloneOk );
    SW_EXPECT_EQUAL( 250, dstActor._health );
    SW_EXPECT_NEAR_EQUAL( 12.0f, dstActor._speed, 1e-4f );
}

/**
 * @brief [ReflectionSerializationTest] 중첩 구조체·컨테이너 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, NestedStructAndContainersRoundtrip )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NestedContainerActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );

    sw::NestedContainerActor src;
    src._grid = {
        { 1, 2 },
        { 3, 4, 5 }
    };
    src._namedRows["a"] = { 1.0f, 2.0f };
    src._namedRows["b"] = { 3.5f };
    src._inner._x       = 42;

    sw::vector<uint8> bin;
    sw::BinarySerializer::serialize( &src, *typeInfo, bin );
    sw::NestedContainerActor dstBin;
    SW_EXPECT_TRUE( sw::BinarySerializer::deserialize( &dstBin, *typeInfo, bin.data(), bin.size() ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dstBin._grid.size() );
    SW_EXPECT_EQUAL( 5, dstBin._grid[1][2] );
    SW_EXPECT_EQUAL( 42, dstBin._inner._x );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), dstBin._namedRows["a"].size() );

    const sw::string         json = sw::JsonSerializer::serialize( &src, *typeInfo );
    sw::NestedContainerActor dstJson;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &dstJson, *typeInfo, json ) );
    SW_EXPECT_EQUAL( 4, dstJson._grid[1][1] );
    SW_EXPECT_EQUAL( 42, dstJson._inner._x );
}

/**
 * @brief [ReflectionSerializationTest] 직렬화 3포맷의 정확한 출력을 골든으로 고정합니다.
 * @details 라운드트립 테스트는 디스크 포맷이 바뀌어도 통과하므로, 리팩터 시 바이트 호환을
 *          지키는 안전망으로 정확한 출력 문자열/헥스를 비교합니다.
 *          바이너리 헥스에는 타입/프로퍼티 이름의 FNV-1a 해시가 포함되어 있어(결정적),
 *          레이아웃·해시·부동소수 포맷이 바뀌면 이 테스트가 먼저 잡습니다.
 */
SW_TEST_CASE( ReflectionSerializationTest, GoldenOutputFormatsStable )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::NestedContainerActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );
    if ( typeInfo == nullptr )
        return;

    const sw::NestedContainerActor src = makeGoldenNestedActor();

    // 컨테이너는 자연스러운 JSON 표현으로 나간다: 시퀀스는 배열, 맵은 오브젝트.
    const sw::string kGoldenJson =
        "{\"_grid\":[[1,2],[3,4,5]],\"_namedRows\":{\"a\":[1,2.5],\"b\":[-3.25]},\"_nestedMap\":{\"out\":{\"x\":7}},\"_listInner\":[{\"_x\":11}],\"_mapInner\":{\"m\":{\"_x\":33}},\"_inner\":{\"_x\":42}}";

    const sw::string kGoldenPretty =
        "{\n"
        "    \"_grid\": [\n"
        "        [\n"
        "            1,\n"
        "            2\n"
        "        ],\n"
        "        [\n"
        "            3,\n"
        "            4,\n"
        "            5\n"
        "        ]\n"
        "    ],\n"
        "    \"_namedRows\": {\n"
        "        \"a\": [\n"
        "            1,\n"
        "            2.5\n"
        "        ],\n"
        "        \"b\": [\n"
        "            -3.25\n"
        "        ]\n"
        "    },\n"
        "    \"_nestedMap\": {\n"
        "        \"out\": {\n"
        "            \"x\": 7\n"
        "        }\n"
        "    },\n"
        "    \"_listInner\": [\n"
        "        {\n"
        "            \"_x\": 11\n"
        "        }\n"
        "    ],\n"
        "    \"_mapInner\": {\n"
        "        \"m\": {\n"
        "            \"_x\": 33\n"
        "        }\n"
        "    },\n"
        "    \"_inner\": {\n"
        "        \"_x\": 42\n"
        "    }\n"
        "}";

    const sw::string kGoldenXml =
        "<NestedContainerActor>\n"
        "	<_grid>\n"
        "		<item>\n"
        "			<item>1</item>\n"
        "			<item>2</item>\n"
        "		</item>\n"
        "		<item>\n"
        "			<item>3</item>\n"
        "			<item>4</item>\n"
        "			<item>5</item>\n"
        "		</item>\n"
        "	</_grid>\n"
        "	<_namedRows>\n"
        "		<entry key=\"a\">\n"
        "			<item>1</item>\n"
        "			<item>2.5</item>\n"
        "		</entry>\n"
        "		<entry key=\"b\">\n"
        "			<item>-3.25</item>\n"
        "		</entry>\n"
        "	</_namedRows>\n"
        "	<_nestedMap>\n"
        "		<entry key=\"out\">\n"
        "			<entry key=\"x\">7</entry>\n"
        "		</entry>\n"
        "	</_nestedMap>\n"
        "	<_listInner>\n"
        "		<NestedInner _x=\"11\" />\n"
        "	</_listInner>\n"
        "	<_mapInner>\n"
        "		<entry key=\"m\">\n"
        "			<NestedInner _x=\"33\" />\n"
        "		</entry>\n"
        "	</_mapInner>\n"
        "	<_inner _x=\"42\" />\n"
        "</NestedContainerActor>\n";

    const sw::string kGoldenBinHex =
        "060000005614ce66612cdb3e20000000020000000200000001000000020000000300000003000000"
        "040000000500000086cb7acc7e60e28222000000020000000100000061020000000000803f000020"
        "40010000006201000000000050c04d6e7f33b377b4f91800000001000000030000006f7574010000"
        "000100000078070000004ad2a1f20b4516201c000000010000001400000001000000a27d0b57bfe2"
        "defb040000000b00000088efdd2cd7dd8ffe2100000001000000010000006d1400000001000000a2"
        "7d0b57bfe2defb0400000021000000be55188f1aa2d1f6180000001400000001000000a27d0b57bf"
        "e2defb040000002a000000";

    const sw::string json = sw::JsonSerializer::serialize( &src, *typeInfo );
    if ( json != kGoldenJson )
        std::fprintf( stdout, "[golden json]\n  expected: %s\n  actual  : %s\n", kGoldenJson.c_str(), json.c_str() );
    SW_EXPECT_TRUE( json == kGoldenJson );

    const sw::string pretty = sw::JsonSerializer::serializePretty( &src, *typeInfo, 4 );
    if ( pretty != kGoldenPretty )
        std::fprintf( stdout, "[golden pretty]\n---expected---\n%s\n---actual---\n%s\n", kGoldenPretty.c_str(), pretty.c_str() );
    SW_EXPECT_TRUE( pretty == kGoldenPretty );

    const sw::string xml = sw::XmlSerializer::serialize( &src, *typeInfo );
    if ( xml != kGoldenXml )
        std::fprintf( stdout, "[golden xml]\n---expected---\n%s\n---actual---\n%s\n", kGoldenXml.c_str(), xml.c_str() );
    SW_EXPECT_TRUE( xml == kGoldenXml );

    sw::vector<uint8> bin;
    sw::BinarySerializer::serialize( &src, *typeInfo, bin );
    const sw::string binHex = toHexString( bin );
    if ( binHex != kGoldenBinHex )
        std::fprintf( stdout, "[golden binhex]\n  expected: %s\n  actual  : %s\n", kGoldenBinHex.c_str(), binHex.c_str() );
    SW_EXPECT_TRUE( binHex == kGoldenBinHex );

    // 역방향: 골든 문자열을 다시 읽어 원본과 같은지 (안전망 자체가 유효한지 확인)
    sw::NestedContainerActor back;
    SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &back, *typeInfo, kGoldenJson ) );
    SW_EXPECT_EQUAL( 5, back._grid[1][2] );
    SW_EXPECT_EQUAL( 42, back._inner._x );
}

/**
 * @brief [ReflectionSerializationTest] 스칼라 이스케이프·비트필드·XmlAttribute·versioned 헤더 골든.
 * @details GoldenOutputFormatsStable(중첩 컨테이너)를 보완하는 두 번째 안전망.
 */
SW_TEST_CASE( ReflectionSerializationTest, GoldenOutputFormatsWide )
{
    sw::TypeRegistry& reg = sw::engine::getTypeRegistry();

    const sw::TypeInfo* pScalar = reg.findType( sw::hashed_string( "sw::SampleTestActor" ) );
    const sw::TypeInfo* pBits   = reg.findType( sw::hashed_string( "sw::BitfieldTestActor" ) );
    const sw::TypeInfo* pAttr   = reg.findType( sw::hashed_string( "sw::DefaultValueTestActor" ) );
    SW_ASSERT_TRUE( pScalar != nullptr && pBits != nullptr && pAttr != nullptr );

    // 스칼라 + 문자열 이스케이프(JSON \" \\ \n / XML 속성 엔티티 이스케이프 &quot; &#10;)
    sw::SampleTestActor scalar;
    scalar._hp   = -7;
    scalar._name = "a\"b\\c\nd";
    SW_EXPECT_TRUE( checkGoldenSet(
        "scalar", &scalar, *pScalar,
        "{\"_hp\":-7,\"_name\":\"a\\\"b\\\\c\\nd\"}",
        "<SampleTestActor _hp=\"-7\" _name=\"a&quot;b\\c&#10;d\" />\n",
        "02000000266feed8bfe2defb04000000f9ffffffcd98c89d3865c1170b000000070000006122625c630a64",
        "{\"_schemaVersion\":7,\"_hp\":-7,\"_name\":\"a\\\"b\\\\c\\nd\"}",
        "0700000002000000266feed8bfe2defb04000000f9ffffffcd98c89d3865c1170b000000070000006122625c630a64" ) );

    // 비트필드(: 1) → JSON/XML true|false, Binary 01|00
    sw::BitfieldTestActor bits;
    bits._bActive       = SW_TRUE;
    bits._bInvulnerable = SW_FALSE;
    bits._bCanJump      = SW_TRUE;
    bits._score         = 777;
    SW_EXPECT_TRUE( checkGoldenSet(
        "bits", &bits, *pBits,
        "{\"_bActive\":true,\"_bInvulnerable\":false,\"_bCanJump\":true,\"_score\":777}",
        "<BitfieldTestActor _bActive=\"true\" _bInvulnerable=\"false\" _bCanJump=\"true\" _score=\"777\" />\n",
        "0400000046dd8356dbf29d1901000000012335d1a0dbf29d1901000000002e8c2fdadbf29d190100000001bc771186bfe2defb0400000009030000",
        "{\"_schemaVersion\":7,\"_bActive\":true,\"_bInvulnerable\":false,\"_bCanJump\":true,\"_score\":777}",
        "070000000400000046dd8356dbf29d1901000000012335d1a0dbf29d1901000000002e8c2fdadbf29d190100000001bc771186bfe2defb0400000009030000" ) );

    // XmlAttribute 플래그 프로퍼티
    sw::DefaultValueTestActor attr;
    attr._mana  = 12;
    attr._title = "Sir";
    SW_EXPECT_TRUE( checkGoldenSet(
        "attr", &attr, *pAttr,
        "{\"_mana\":12,\"_title\":\"Sir\"}",
        "<DefaultValueTestActor _mana=\"12\" _title=\"Sir\" />\n",
        "0200000089a752a5bfe2defb040000000c00000068fdd9173865c1170700000003000000536972",
        "{\"_schemaVersion\":7,\"_mana\":12,\"_title\":\"Sir\"}",
        "070000000200000089a752a5bfe2defb040000000c00000068fdd9173865c1170700000003000000536972" ) );
}

/**
 * @brief [ReflectionSerializationTest] Reflection RPC 팩·호출
 */
SW_TEST_CASE( ReflectionSerializationTest, ReflectionRpcPackInvoke )
{
    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::RpcDemoActor" ) );
    SW_ASSERT_TRUE( typeInfo != nullptr );
    const sw::FunctionInfo* fn = typeInfo->findMethod( sw::hashed_string( "applyDamage" ) );
    SW_ASSERT_TRUE( fn != nullptr );
    SW_EXPECT_TRUE( fn->_metadata._netRole == sw::FunctionNetRole::Server );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( fn->_metadata._bReliable ) );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Apply Damage" ), fn->_metadata._displayName );
    SW_EXPECT_EQUAL( sw::string( "Subtracts amount from HP" ), fn->_metadata._tooltip );
    SW_EXPECT_EQUAL( sw::string( "Combat" ), fn->_metadata._category );
#endif

    sw::RpcDemoActor actor;
    actor._hp = 100;
    sw::TaskArgs args;
    args.add( int32{ 25 } );

    // 봉투를 싸고(pack) 다시 풀어(invoke) 왕복시킨다 — 예전엔 이 두 줄을 묶은 packAndInvoke 가
    // 엔진 API 에 있었지만 부르는 곳이 이 테스트뿐이라 테스트로 내렸다.
    sw::RpcEnvelope envelope;
    SW_ASSERT_TRUE( sw::ReflectionRpc::packCall( envelope, sw::hashed_string( "sw::RpcDemoActor" ),
                                                 sw::hashed_string( "applyDamage" ), args ) );
    sw::ReflectionRpc::unpackAndInvoke( &actor, envelope );
    SW_EXPECT_EQUAL( 75, actor._hp );
}

/**
 * @brief [ReflectionSerializationTest] 인자 타입이 어긋난 RPC 봉투는 호출되지 않는다
 * @details 봉투는 인자마다 **보낸 쪽의 타입 해시**를 싣는데 풀 때는 그것을 버리고(`(void)typeNameHash`)
 *          받는 쪽 시그니처만 보고 읽었다. 시그니처가 어긋난 채 주고받으면(빌드가 다르거나 모듈이
 *          핫리로드된 뒤, 또는 봉투가 망가진 채로) 같은 바이트를 다른 타입으로 읽어 **터지지 않고
 *          값만 조용히 달라진다** — `float32 1.5f` 를 `int32` 로 읽으면 `1069547520` 이 되는 식이다.
 */
SW_TEST_CASE( ReflectionSerializationTest, RpcRejectsAnArgumentTypeThatDoesNotMatchTheWire )
{
    sw::RpcDemoActor actor;
    actor._hp = 100;

    sw::TaskArgs args;
    args.add( int32{ 25 } );

    sw::RpcEnvelope envelope;
    SW_ASSERT_TRUE( sw::ReflectionRpc::packCall( envelope, sw::hashed_string( "sw::RpcDemoActor" ),
                                                 sw::hashed_string( "applyDamage" ), args ) );

    // 봉투 앞머리는 [인자 수][타입 해시][크기][payload] 다 — 첫 인자의 타입 해시만 바꾼다.
    SW_ASSERT_TRUE( envelope._argumentBytes.size() >= sizeof( uint32 ) * 3 );
    const uint32 wrongTypeHash = sw::hashed_string( "float32" ).getHash();
    sw::Memory::copy( envelope._argumentBytes.data() + sizeof( uint32 ), &wrongTypeHash, sizeof( uint32 ) );

    {
        test::ScopedLogSuppressor suppressor;
        sw::ReflectionRpc::unpackAndInvoke( &actor, envelope );
    }
    SW_EXPECT_TRUE_MSG( actor._hp == 100, "전선이 말한 타입과 다른데도 인자를 그대로 읽어 호출했습니다" );
}

/**
 * @brief [ReflectionSerializationTest] REFLECT Abstract/Static
 */
SW_TEST_CASE( ReflectionSerializationTest, ReflectAbstractAndStatic )
{
    const sw::TypeInfo* abstractInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AbstractDemoBase" ) );
    SW_ASSERT_TRUE( abstractInfo != nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( abstractInfo->_bAbstract ) );
    SW_EXPECT_TRUE( abstractInfo->canConstruct() == false );
    SW_EXPECT_TRUE( abstractInfo->findMethod( sw::hashed_string( "$ctor" ) ) == nullptr );

    const sw::TypeInfo* staticInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::StaticDemoLibrary" ) );
    SW_ASSERT_TRUE( staticInfo != nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( staticInfo->_bStatic ) );
    SW_EXPECT_TRUE( staticInfo->canConstruct() == false );

    const sw::FunctionInfo* doubleFn = staticInfo->findMethod( sw::hashed_string( "doubleInt" ) );
    SW_ASSERT_TRUE( doubleFn != nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( doubleFn->_metadata._bStatic ) );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Double Int" ), doubleFn->_metadata._displayName );
    SW_EXPECT_EQUAL( sw::string( "Returns value * 2" ), doubleFn->_metadata._tooltip );
    SW_EXPECT_EQUAL( sw::string( "Math" ), doubleFn->_metadata._category );
#endif

    sw::TaskArgs args;
    args.add( int32{ 21 } );
    const sw::TaskValue result = doubleFn->_invoker( nullptr, args );
    SW_EXPECT_EQUAL( 42, result.getValue<int32>() );
}

/**
 * @brief [ReflectionSerializationTest] JSON 이스케이프 라운드트립
 */
SW_TEST_CASE( ReflectionSerializationTest, JsonEscapeUnescapeRoundtrip )
{
    const sw::string raw     = "line\n\t\"quote\"\\slash";
    const sw::string escaped = sw::JsonSerializer::escapeString( raw );
    SW_EXPECT_TRUE( escaped.find( '\n' ) == sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( '\t' ) == sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( "\\\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( escaped.find( "\\\\" ) != sw::string::npos );
    SW_EXPECT_EQUAL( raw, sw::JsonSerializer::unescapeString( escaped ) );

    const sw::string extracted =
        sw::JsonSerializer::extractStringField( R"({"Title":"Hero","HP":"10"})", "title", true );
    SW_EXPECT_EQUAL( sw::string( "Hero" ), extracted );
    const sw::string missing =
        sw::JsonSerializer::extractStringField( R"({"Title":"Hero"})", "title", false );
    SW_EXPECT_TRUE( missing.empty() );
}

/**
 * @brief [ReflectionSerializationTest] ReflectAny 다형성
 */
SW_TEST_CASE( ReflectionSerializationTest, ReflectAnyPolymorphic )
{
    const sw::TypeInfo* payloadType =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::PolyPayloadA" ) );
    SW_ASSERT_TRUE( payloadType != nullptr );
    sw::PolyPayloadA payload;
    payload._a         = 9;
    sw::ReflectAny any = sw::ReflectAny::makeFrom( *payloadType, &payload );
    SW_EXPECT_TRUE( any.empty() == false );

    sw::PolyPayloadA out{};
    SW_EXPECT_TRUE( any.tryGetFrom( *payloadType, &out ) );
    SW_EXPECT_EQUAL( 9, out._a );

    const sw::TypeInfo* typeInfo =
        sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::AssetPathActor" ) );
    if ( typeInfo == nullptr )
        SW_TEST_SKIP( "AssetPathActor type not registered" );
    const sw::PropertyInfo* albedo = typeInfo->findProperty( sw::hashed_string( "_albedo" ) );
    SW_ASSERT_TRUE( albedo != nullptr );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( albedo->_metadata._bAssetPath ) );
}

/**
 * @brief [ReflectionSerializationTest] `set` 프로퍼티가 **세 포맷 모두** 왕복한다
 * @details 예전에는 **프로세스가 죽었다.** 역직렬화가 시퀀스 컨테이너를 "자리를 먼저 만들고
 *          (`addElementDefault`) 그 자리에 제자리로 쓴다(`getElement`)" 로만 채웠는데, `set` 의 원소는
 *          곧 정렬 키라 트리에 들어간 뒤 값을 바꾸면 정렬 불변식이 깨진다. 증상은 그 자리에서 나지
 *          않고 나중에 엉뚱한 곳에서 터져, 원인을 찾기 어려운 모양이었다.
 *          이제 컨테이너가 `appendElement` 로 **넣는 방법을 스스로 정한다** — `set` 은 다 읽은 뒤 insert 한다.
 * @note 값을 **정렬되지 않은 순서로** 넣는 것이 중요하다. 순서대로 넣으면 제자리 쓰기 구현도 우연히
 *       통과할 수 있다(각 원소가 마침 트리의 끝에 붙는다).
 */
SW_TEST_CASE( ReflectionSerializationTest, SetPropertyRoundTripsInEveryFormat )
{
    struct SetHolder
    {
        sw::set<int32> _values{};
    };

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "SetRoundTripHolder" );
    info._fullyQualifiedName = sw::hashed_string( "sw::SetRoundTripHolder" );
    info._size               = sizeof( SetHolder );
    info._listProperty       = {
        { sw::hashed_string( "_values" ), sw::hashed_string( "int32" ),
         SW_OFFSET_OF( SetHolder, _values ), true, sw::ContainerKind::Sequence,
         sw::hashed_string( "int32" ), sw::hashed_string(), sw::make_shared<sw::SetWrapper<sw::set<int32>>>() }
    };

    SetHolder source{};
    source._values = { 42, 5, 17 }; // 정렬되지 않은 순서로 적는다(위 @note 참고).

    const auto expectContents = []( const SetHolder& restored, const utf8* pWhat )
    {
        SW_EXPECT_TRUE_MSG( restored._values.size() == 3, pWhat );
        SW_EXPECT_TRUE_MSG( restored._values.find( 5 ) != restored._values.end(), pWhat );
        SW_EXPECT_TRUE_MSG( restored._values.find( 17 ) != restored._values.end(), pWhat );
        SW_EXPECT_TRUE_MSG( restored._values.find( 42 ) != restored._values.end(), pWhat );
    };

    // 1) Binary
    {
        sw::vector<uint8> bytes;
        sw::BinarySerializer::serialize( &source, info, bytes );
        SetHolder restored{};
        SW_ASSERT_TRUE( sw::BinarySerializer::deserialize( &restored, info, bytes.data(), bytes.size() ) );
        expectContents( restored, "Binary 왕복이 set 을 잃었습니다" );
    }

    // 2) JSON
    {
        const sw::string json = sw::JsonSerializer::serialize( &source, info );
        SetHolder        restored{};
        SW_ASSERT_TRUE( sw::JsonSerializer::deserialize( &restored, info, json ) );
        expectContents( restored, "JSON 왕복이 set 을 잃었습니다" );
    }

    // 3) XML
    {
        const sw::string xml = sw::XmlSerializer::serialize( &source, info );
        SetHolder        restored{};
        SW_ASSERT_TRUE( sw::XmlSerializer::deserialize( &restored, info, xml ) );
        expectContents( restored, "XML 왕복이 set 을 잃었습니다" );
    }
}
