// ObjectStateSerializerTest — 모르는 타입(MissingComponent)을 가리키는 컴포넌트 핸들이 로드 · 저장 왕복에서 사라지지 않는다(nogpu).
#include "pch.h"

#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Serialization/Tests/HandleTestComponents.h"
#include "Engine/Reflection/TypeInfo.h"

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
    fileOptions._bOmitExternalParent  = SW_TRUE; // 파일 형태(`{저장 id, 키}`)로 적는다
    const expected<string> savedState = ObjectStateSerializer::saveToXMLString( pObject, fileOptions );
    SW_REQUIRE_TRUE( savedState.has_value() );
    const string                             saved = *savedState;
    StringBuilder<constant::buffer::kMax128> keyBuilder;
    keyBuilder.appendFormat( "0:%#", ComponentStableKey::makeKey( pObject->getComponent<HandleTargetTestComponent>() ) );
    const string key = string( keyBuilder.view() );
    SW_REQUIRE_TRUE( saved.find( key ) != string::npos );

    // 대상 타입을 모르는 것으로 만든다(모듈이 안 뜬 것과 같다) — 원소 태그 이름만 바꾸고 핸들 글은 그대로 둔다.
    const string                             typeName = HandleTargetTestComponent::StaticType()->_name.c_str();
    StringBuilder<constant::buffer::kMax128> typeOpen;
    typeOpen.appendFormat( "<%#", typeName );
    StringBuilder<constant::buffer::kMax128> typeClose;
    typeClose.appendFormat( "</%#>", typeName );
    StringBuilder<constant::buffer::kMax128> goneOpen;
    goneOpen.appendFormat( "<%#Gone", typeName );
    StringBuilder<constant::buffer::kMax128> goneClose;
    goneClose.appendFormat( "</%#Gone>", typeName );
    const string gone = StringUtil::replace( StringUtil::replace( saved, typeOpen.view(), goneOpen.view() ), typeClose.view(), goneClose.view() );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( pObject, gone, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    SW_REQUIRE_NOT_NULL( pObject->getComponent<MissingComponent>() );
    HandleHolderTestComponent* pHolder = pObject->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    SW_EXPECT_TRUE( pHolder->_target.isValid() );                   // 비우지 않았다
    SW_EXPECT_NULL( manager.resolveComponent( pHolder->_target ) ); // 다만 찾으면 아무것도 아니다(다른 타입을 가리키지 않는다)

    const expected<string> resavedState = ObjectStateSerializer::saveToXMLString( pObject, fileOptions );
    SW_REQUIRE_TRUE( resavedState.has_value() );
    const string resaved = *resavedState;
    SW_EXPECT_TRUE( resaved.find( key ) != string::npos );
    SW_EXPECT_TRUE( resaved.find( goneOpen.c_str() ) != string::npos );

    // 타입이 돌아오면 다시 저장한 글이 원래 컴포넌트를 찾는다.
    const string restored = StringUtil::replace( StringUtil::replace( resaved, goneOpen.view(), typeOpen.view() ), goneClose.view(), typeClose.view() );
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

/**
 * @brief [ObjectStateSerializerTest] 파일에 적힌 핸들은 대상의 고정 지역 id(`_localID`)를 따른다 — 같은 이름의 형제를 앞에 끼워도 같은 컴포넌트를 찾는다(감사 A-2)
 * @details 예전 키 `이름#n` 은 순번이라, 앞에 같은 타입을 끼우면 쌓인 세이브 · 씬의 핸들이 경고 없이 끼운 컴포넌트로 갔다.
 */
SW_TEST_CASE( ObjectStateSerializerTest, SavedHandleSurvivesSiblingInsertion )
{
    GameObjectManager manager;
    GameObject*       pObject = createHolderObject( manager );
    SW_REQUIRE_NOT_NULL( pObject );
    ObjectSaveOptions fileOptions{ SerializePurpose::Authoring };
    fileOptions._bOmitExternalParent  = SW_TRUE;
    const expected<string> savedState = ObjectStateSerializer::saveToXMLString( pObject, fileOptions );
    SW_REQUIRE_TRUE( savedState.has_value() );
    const string saved = *savedState;

    // 같은 타입의 컴포넌트를 맨 앞에 끼운다(디자이너가 나중에 더했다 — 지역 id 는 아직 없다).
    const string                             typeName = HandleTargetTestComponent::StaticType()->_name.c_str();
    StringBuilder<constant::buffer::kMax128> typeOpen;
    typeOpen.appendFormat( "<%#", typeName );
    const size_t first = saved.find( typeOpen.c_str() );
    SW_REQUIRE_TRUE( first != string::npos );
    StringBuilder<constant::buffer::kMax128> emptyElement;
    emptyElement.appendFormat( "<%#/>", typeName );
    string inserted = saved;
    inserted.insert( first, emptyElement.c_str(), emptyElement.size() );

    GameObject* pLoaded = manager.createGameObject( hashed_string( "Loaded" ) );
    SW_REQUIRE_NOT_NULL( pLoaded );
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromXMLString( pLoaded, inserted, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    vector<HandleTargetTestComponent*> listTarget;
    for ( Component* pComp : pLoaded->getComponents() )
    {
        if ( pComp != nullptr && pComp->getTypeInfo() == HandleTargetTestComponent::StaticType() )
            listTarget.push_back( static_cast<HandleTargetTestComponent*>( pComp ) );
    }
    SW_REQUIRE_EQUAL( size_t{ 2 }, listTarget.size() );
    const HandleHolderTestComponent* pHolder = pLoaded->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pHolder );
    SW_EXPECT_TRUE( manager.resolveComponent( pHolder->_target ) == listTarget[1] ); // 끼운 것이 아니라 원래 대상
    SW_EXPECT_TRUE( ComponentStableKey::getLocalID( listTarget[0] ) != ComponentStableKey::getLocalID( listTarget[1] ) );
}
