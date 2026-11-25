// ReflectedXMLDocumentTest — 리플렉션 인스턴스로 고친 칸만 원문에 쓴다: 주석 · 다른 칸의 글은 그대로이고, 고친 글을 엔진 직렬화기가 같은 값으로 읽는다.
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Panels/Catalog/Model/ReflectedXMLDocument.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/VFX/VFXEffectDef.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ReflectedXMLDocumentTestInternal
    {
        static constexpr const utf8* kEffect = "<VFXEffectDef>\n"
                                               "\t<!-- 불꽃 -->\n"
                                               "\t<_listEmitter>\n"
                                               "\t\t<VFXEmitterDef _name=\"Fire\" _maxParticles=\"8\" _duration=\"1\" _lifetimeMin=\"1\" _lifetimeMax=\"1\" _velocityMin=\"0, -0.04, 0.01\">\n"
                                               "\t\t\t<_light _maxLights=\"1\" _radius=\"2\" _intensity=\"3\" />\n"
                                               "\t\t</VFXEmitterDef>\n"
                                               "\t\t<VFXEmitterDef _name=\"Smoke\" _maxParticles=\"4\" _duration=\"1\" _lifetimeMin=\"1\" _lifetimeMax=\"1\" />\n"
                                               "\t</_listEmitter>\n"
                                               "</VFXEffectDef>\n";
        static constexpr const utf8* kList   = "_listEmitter";

        static VFXEffectDef& getEffect( ReflectedXMLDocument& document ) { return *static_cast<VFXEffectDef*>( document.getInstance() ); }

        static bool commit( ReflectedXMLDocument& document, uint32 index, const utf8* pProperty )
        {
            const PropertyInfo* pProp = VFXEmitterDef::StaticType()->findPropertyInHierarchy( hashed_string( pProperty ) );
            return pProp != nullptr && document.commitItemProperty( kList, index, &getEffect( document )._listEmitter[index], *VFXEmitterDef::StaticType(), *pProp );
        }

        static VFXEffectDef read( const ReflectedXMLDocument& document )
        {
            VFXEffectDef effect;
            SW_EXPECT_TRUE( XMLSerializer::deserialize( &effect, *VFXEffectDef::StaticType(), document.getText() ) );
            return effect;
        }

        /** @brief 물려받은 문서의 글을 런타임과 같은 길(펼치기)로 다시 읽습니다. */
        static VFXEffectDef readInherited( const ReflectedXMLDocument& document )
        {
            DataInheritanceResourceSource source;
            string                        flatText;
            VFXEffectDef                  effect;
            SW_EXPECT_TRUE( DataInheritance::flatten( document.getText(), "test", DataInheritanceSchema::makeFromType( *VFXEffectDef::StaticType() ), source, flatText ) );
            SW_EXPECT_TRUE( XMLSerializer::deserialize( &effect, *VFXEffectDef::StaticType(), flatText ) );
            return effect;
        }

        static optional<uint32> findEmitter( const VFXEffectDef& effect, string_view name )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( effect._listEmitter.size() ); ++index )
            {
                if ( string_view{ effect._listEmitter[index]._name } == name )
                    return index;
            }
            return nullopt;
        }
    };
} // namespace

/** @brief [ReflectedXMLDocumentTest] 속성 · 자식 칸을 고치면 그 글만 바뀌고 주석 · 손 배치가 남으며, 다시 읽으면 고친 값이다 */
SW_TEST_CASE( ReflectedXMLDocumentTest, CommitsOnlyTheEditedProperty )
{
    using Internal = ReflectedXMLDocumentTestInternal;
    ReflectedXMLDocument document;
    SW_REQUIRE_TRUE( document.load( *VFXEffectDef::StaticType(), Internal::kEffect, "test" ).has_value() );
    SW_REQUIRE_TRUE( document.isEditable() );
    SW_EXPECT_FALSE( document.isDirty() );

    Internal::getEffect( document )._listEmitter[0]._maxParticles = 32;
    SW_EXPECT_TRUE( Internal::commit( document, 0, "_maxParticles" ) );
    SW_EXPECT_TRUE( document.isDirty() );
    SW_EXPECT_TRUE( StringUtil::contains( document.getText(), "_maxParticles=\"32\"", true ) );
    SW_EXPECT_TRUE( StringUtil::contains( document.getText(), "<!-- 불꽃 -->", true ) );
    SW_EXPECT_TRUE( StringUtil::contains( document.getText(), "_velocityMin=\"0, -0.04, 0.01\"", true ) ); // 고치지 않은 칸은 손 배치 그대로
    SW_EXPECT_FALSE( Internal::commit( document, 0, "_maxParticles" ) );                                   // 같은 글이면 쓰지 않는다

    Internal::getEffect( document )._listEmitter[0]._light._radius = 5.0f;
    SW_EXPECT_TRUE( Internal::commit( document, 0, "_light" ) );
    // 적혀 있지 않고 기본값이면 적지 않는다
    SW_EXPECT_FALSE( Internal::commit( document, 1, "_spawnRate" ) );
    Internal::getEffect( document )._listEmitter[1]._spawnRate = 12.0f;
    SW_EXPECT_TRUE( Internal::commit( document, 1, "_spawnRate" ) );

    const VFXEffectDef read = Internal::read( document );
    SW_REQUIRE_EQUAL( size_t{ 2 }, read._listEmitter.size() );
    SW_EXPECT_EQUAL( 32u, read._listEmitter[0]._maxParticles );
    SW_EXPECT_NEAR( 5.0f, read._listEmitter[0]._light._radius, 1.0e-4f );
    SW_EXPECT_NEAR( 12.0f, read._listEmitter[1]._spawnRate, 1.0e-4f );
}

/** @brief [ReflectedXMLDocumentTest] 복사한 원소는 겹치지 않는 이름을 받고, 빼면 사라지며, 앞 원문을 적용하면(되돌리기) 인스턴스까지 돌아온다 */
SW_TEST_CASE( ReflectedXMLDocumentTest, DuplicateRemoveAndUndoText )
{
    using Internal = ReflectedXMLDocumentTestInternal;
    ReflectedXMLDocument document;
    SW_REQUIRE_TRUE( document.load( *VFXEffectDef::StaticType(), Internal::kEffect, "test" ).has_value() );
    SW_EXPECT_TRUE( document.duplicateItem( Internal::kList, 0, "_name" ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, Internal::getEffect( document )._listEmitter.size() );
    SW_EXPECT_EQUAL( string( "Fire_copy" ), Internal::getEffect( document )._listEmitter[1]._name );
    SW_EXPECT_TRUE( document.duplicateItem( Internal::kList, 0, "_name" ) );
    SW_EXPECT_EQUAL( string( "Fire_copy_copy" ), Internal::getEffect( document )._listEmitter[1]._name );
    SW_EXPECT_TRUE( document.removeItem( Internal::kList, 3 ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, Internal::getEffect( document )._listEmitter.size() );
    SW_EXPECT_FALSE( document.removeItem( Internal::kList, 9 ) );

    document.applyText( Internal::kEffect );
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_EQUAL( size_t{ 2 }, Internal::getEffect( document )._listEmitter.size() );
}

/** @brief [ReflectedXMLDocumentTest] `_base` 로 물려받은 문서는 부모를 펼친 값을 고치고 다른 칸만 적으며, 부모 값으로 되돌린 칸은 글에서 빠진다 */
SW_TEST_CASE( ReflectedXMLDocumentTest, InheritedDocumentWritesOnlyTheDifference )
{
    using Internal = ReflectedXMLDocumentTestInternal;
    ReflectedXMLDocument document;
    SW_REQUIRE_TRUE( document.load( *VFXEffectDef::StaticType(), "<VFXEffectDef _base=\"engine/vfx/sparks.vfx.xml\" />", "test" ).has_value() );
    SW_EXPECT_EQUAL( string( "engine/vfx/sparks.vfx.xml" ), document.getBaseID() );
    SW_REQUIRE_TRUE( document.isEditable() );
    const optional<uint32> emitterIndex = Internal::findEmitter( Internal::getEffect( document ), "Sparks" );
    SW_REQUIRE_TRUE( emitterIndex.has_value() );
    SW_EXPECT_TRUE( document.isInheritedItem( Internal::kList, *emitterIndex ) );
    const uint32 parentCount = Internal::getEffect( document )._listEmitter[*emitterIndex]._maxParticles;

    SW_EXPECT_FALSE( Internal::commit( document, *emitterIndex, "_maxParticles" ) ); // 부모와 같으면 적지 않는다
    Internal::getEffect( document )._listEmitter[*emitterIndex]._maxParticles = parentCount + 8;
    SW_EXPECT_TRUE( Internal::commit( document, *emitterIndex, "_maxParticles" ) );
    SW_EXPECT_TRUE( StringUtil::contains( document.getText(), "_name=\"Sparks\"", true ) );
    SW_EXPECT_FALSE( StringUtil::contains( document.getText(), "_duration", true ) ); // 고치지 않은 칸은 물려받는다
    const VFXEffectDef     read      = Internal::readInherited( document );
    const optional<uint32> readIndex = Internal::findEmitter( read, "Sparks" );
    SW_REQUIRE_TRUE( readIndex.has_value() );
    SW_EXPECT_EQUAL( parentCount + 8, read._listEmitter[*readIndex]._maxParticles );
    SW_EXPECT_EQUAL( Internal::getEffect( document )._listEmitter.size(), read._listEmitter.size() );

    Internal::getEffect( document )._listEmitter[*emitterIndex]._maxParticles = parentCount;
    SW_EXPECT_TRUE( Internal::commit( document, *emitterIndex, "_maxParticles" ) );
    SW_EXPECT_FALSE( StringUtil::contains( document.getText(), "_maxParticles", true ) );
}

/** @brief [ReflectedXMLDocumentTest] 물려받은 원소의 복사는 이 파일의 새 원소로 원본 뒤에 서고, 빼면 부모의 원소는 `_op="Remove"` · 이 파일의 원소는 글에서 사라진다 */
SW_TEST_CASE( ReflectedXMLDocumentTest, InheritedItemsDuplicateAndRemove )
{
    using Internal = ReflectedXMLDocumentTestInternal;
    ReflectedXMLDocument document;
    SW_REQUIRE_TRUE( document.load( *VFXEffectDef::StaticType(), "<VFXEffectDef _base=\"engine/vfx/sparks.vfx.xml\" />", "test" ).has_value() );
    const size_t           parentCount  = Internal::getEffect( document )._listEmitter.size();
    const optional<uint32> emitterIndex = Internal::findEmitter( Internal::getEffect( document ), "Sparks" );
    SW_REQUIRE_TRUE( emitterIndex.has_value() );

    SW_REQUIRE_TRUE( document.duplicateItem( Internal::kList, *emitterIndex, "_name" ) );
    SW_REQUIRE_EQUAL( parentCount + 1, Internal::getEffect( document )._listEmitter.size() );
    SW_EXPECT_EQUAL( string( "Sparks_copy" ), Internal::getEffect( document )._listEmitter[*emitterIndex + 1]._name );
    SW_EXPECT_FALSE( document.isInheritedItem( Internal::kList, *emitterIndex + 1 ) );

    SW_EXPECT_TRUE( document.removeItem( Internal::kList, *emitterIndex + 1 ) );
    SW_EXPECT_FALSE( StringUtil::contains( document.getText(), "Sparks_copy", true ) );
    SW_EXPECT_TRUE( document.removeItem( Internal::kList, *emitterIndex ) );
    SW_EXPECT_TRUE( StringUtil::contains( document.getText(), "_op=\"Remove\"", true ) );
    SW_EXPECT_FALSE( Internal::findEmitter( Internal::readInherited( document ), "Sparks" ).has_value() );
    SW_EXPECT_FALSE( Internal::findEmitter( Internal::getEffect( document ), "Sparks" ).has_value() );
}
