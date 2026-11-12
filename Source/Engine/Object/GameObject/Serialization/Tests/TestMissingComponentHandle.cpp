// ObjectStateSerializerTest — 모르는 타입(MissingComponent)을 가리키는 컴포넌트 핸들이 로드 · 저장 왕복에서 사라지지 않는다(nogpu).
#include "pch.h"

#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Serialization/Tests/HandleTestComponents.h"

using namespace sw;

namespace
{
    /** @brief 대상과 그것을 가리키는 핸들을 가진 오브젝트를 만듭니다. */
    GameObject* createHolderObject( GameObjectManager& manager )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "Holder" ) );
        if ( pObject == nullptr )
            return nullptr;
        HandleTargetTestComponent* pTarget = pObject->addComponent<HandleTargetTestComponent>();
        HandleHolderTestComponent* pHolder = pObject->addComponent<HandleHolderTestComponent>();
        if ( pTarget == nullptr || pHolder == nullptr )
            return nullptr;
        pHolder->_target = TComponentHandle<HandleTargetTestComponent>{ pTarget };
        return pObject;
    }

    /** @brief 바이너리의 다형 원소 이름(`[길이][이름]`)만 같은 길이의 다른 이름으로 바꿉니다. 본문 안의 같은 글은 건드리지 않는다. */
    void replaceElementName( vector<uint8>& inoutBytes, string_view from, string_view to )
    {
        vector<uint8> pattern;
        LittleEndianUtil::appendUint32( pattern, static_cast<uint32>( from.size() ) );
        pattern.insert( pattern.end(), reinterpret_cast<const uint8*>( from.data() ), reinterpret_cast<const uint8*>( from.data() ) + from.size() );
        for ( size_t offset = 0; offset + pattern.size() <= inoutBytes.size(); ++offset )
        {
            if ( Memory::compare( inoutBytes.data() + offset, pattern.data(), pattern.size() ) != 0 )
                continue;
            Memory::copy( inoutBytes.data() + offset + sizeof( uint32 ), to.data(), to.size() );
        }
    }
} // namespace

/**
 * @brief [ObjectStateSerializerTest] XML: 저장 → 대상 타입 없이 로드 → 다시 저장해도 핸들 글이 그대로이고, 타입이 돌아오면 다시 그 컴포넌트를 찾는다
 * @details 예전에는 키로 대상을 찾지 못하면 핸들을 비워(`ComponentHandle{}`) 다음 저장이 `0:0` 을 적었다 — 모듈이 안 뜬 채 에디터가 저장하면 참조가 사라졌다.
 */
SW_TEST_CASE( ObjectStateSerializerTest, HandleToMissingComponentSurvivesSaveRoundTrip )
{
    SW_EXPECT_LOG( "ObjectStateSerializer.ComponentUnknownTypeKept" );
    SW_EXPECT_LOG( "Object.HandleKeptForMissingComponent" );
    GameObjectManager manager;
    GameObject*       pObject = createHolderObject( manager );
    SW_REQUIRE_NOT_NULL( pObject );

    ObjectSaveOptions fileOptions{ SerializePurpose::Authoring };
    fileOptions._bOmitExternalParent = SW_TRUE; // 파일 형태(`{저장 id, 키}`)로 적는다
    const string saved               = ObjectStateSerializer::saveToXMLString( pObject, fileOptions );
    const string key                 = "0:" + ComponentStableKey::makeKey( pObject->getComponent<HandleTargetTestComponent>() );
    SW_REQUIRE_TRUE( saved.find( key ) != string::npos );

    // 대상 타입을 모르는 것으로 만든다(모듈이 안 뜬 것과 같다) — 원소 태그 이름만 바꾸고 핸들 글은 그대로 둔다.
    const string typeName = HandleTargetTestComponent::StaticType()->_name.c_str();
    const string goneName = typeName + "Gone";
    const string gone     = StringUtil::replace( StringUtil::replace( saved, "<" + typeName, "<" + goneName ), "</" + typeName + ">", "</" + goneName + ">" );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( pObject, gone, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    SW_REQUIRE_NOT_NULL( pObject->getComponent<MissingComponent>() );
    HandleHolderTestComponent* pHolder = pObject->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    SW_EXPECT_TRUE( pHolder->_target.isValid() );                   // 비우지 않았다
    SW_EXPECT_NULL( manager.resolveComponent( pHolder->_target ) ); // 다만 찾으면 아무것도 아니다(다른 타입을 가리키지 않는다)

    const string resaved = ObjectStateSerializer::saveToXMLString( pObject, fileOptions );
    SW_EXPECT_TRUE( resaved.find( key ) != string::npos );
    SW_EXPECT_TRUE( resaved.find( "<" + goneName ) != string::npos );

    // 타입이 돌아오면 다시 저장한 글이 원래 컴포넌트를 찾는다.
    const string restored = StringUtil::replace( StringUtil::replace( resaved, "<" + goneName, "<" + typeName ), "</" + goneName + ">", "</" + typeName + ">" );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( pObject, restored, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    pHolder = pObject->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    const HandleTargetTestComponent* pTarget = manager.resolveComponent( pHolder->_target );
    SW_EXPECT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget == pObject->getComponent<HandleTargetTestComponent>() );
}

/**
 * @brief [ObjectStateSerializerTest] 바이너리(세이브): 같은 왕복에서 핸들의 키 해시가 그대로이고, 타입이 돌아오면 다시 그 컴포넌트를 찾는다
 */
SW_TEST_CASE( ObjectStateSerializerTest, HandleToMissingComponentSurvivesBinarySaveRoundTrip )
{
    SW_EXPECT_LOG( "ObjectStateSerializer.ComponentUnknownTypeKept" );
    SW_EXPECT_LOG( "Object.HandleKeptForMissingComponent" );
    GameObjectManager manager;
    GameObject*       pObject = createHolderObject( manager );
    SW_REQUIRE_NOT_NULL( pObject );

    const ObjectSaveOptions saveOptions{ SerializePurpose::SaveGame }; // 세이브는 늘 파일 형태다
    vector<uint8>           saved;
    SW_REQUIRE_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pObject, saved, saveOptions ) );

    // 같은 길이의 다른 이름으로 바꿔 대상 타입을 모르게 한다.
    const string typeName = HandleTargetTestComponent::StaticType()->_name.c_str();
    string       goneName = typeName;
    goneName[0]           = 'X';
    replaceElementName( saved, typeName, goneName );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromBinaryBuffer( pObject, saved.data(), saved.size(), ObjectLoadContext{ SerializePurpose::SaveGame } ) != 0 );
    SW_REQUIRE_NOT_NULL( pObject->getComponent<MissingComponent>() );
    HandleHolderTestComponent* pHolder = pObject->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    SW_EXPECT_TRUE( pHolder->_target.isValid() );
    SW_EXPECT_NULL( manager.resolveComponent( pHolder->_target ) );

    vector<uint8> resaved;
    SW_REQUIRE_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pObject, resaved, saveOptions ) );
    replaceElementName( resaved, goneName, typeName );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromBinaryBuffer( pObject, resaved.data(), resaved.size(), ObjectLoadContext{ SerializePurpose::SaveGame } ) != 0 );
    pHolder = pObject->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    const HandleTargetTestComponent* pTarget = manager.resolveComponent( pHolder->_target );
    SW_EXPECT_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget == pObject->getComponent<HandleTargetTestComponent>() );
}
