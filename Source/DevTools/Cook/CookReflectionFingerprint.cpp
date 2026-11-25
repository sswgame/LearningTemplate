#include "pch.h"

#include "DevTools/Cook/CookReflectionFingerprint.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedInstance.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/UI/WidgetTree/Widget.h"

namespace sw
{
    namespace
    {
        struct CookReflectionFingerprintInternal
        {
            /** @brief 잎 칸 하나의 값입니다. 경로는 칸 이름 해시의 사슬이라 같은 자리면 같다. */
            struct DefaultValue
            {
                uint64 _path{ 0 };
                uint64 _value{ 0 };
            };

            /** @brief @p pInstance 의 저작 칸(구조체 값 · 구조체 원소 안까지)을 바이트로 옮겨 @p outListValue 에 담습니다. 소유 포인터 목록과 옮길 수 없는 칸은 뺍니다. */
            static void collectValues( const TypeInfo& type, const void* pInstance, const TypeRegistry& registry, vector<DefaultValue>& outListValue )
            {
                const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
                ReflectVisitOptions     options;
                options._bIntoValues = SW_TRUE;
                const ReflectPropertyVisitor propertyVisitor{ registry, options };
                vector<uint8>                bytes;
                (void)propertyVisitor.visitProperties( type, pInstance, [&]( const ReflectVisitConstField& field )
                {
                    const PropertyInfo& prop       = field._property;
                    const bool          bContainer = prop._bIsContainer == SW_TRUE;
                    if ( prop._metadata->isAuthored() == false || ( bContainer && SerializerUtil::isOwnedPointerElementType( prop._elementTypeName ) ) )
                        return ReflectVisitFlow::SkipChildren;
                    const uint64 path = HashUtil::combine( HashUtil::combine( field._depth, field._owner._fullyQualifiedName.getHash() ), prop.getNameHash() );
                    // 처리기가 없는 구조체 값은 안쪽 칸이 값이다. 구조체 원소의 시퀀스는 개수를 접고 안쪽 칸으로 내려간다.
                    const bool                       bStructValue = SerializerUtil::findNestedObjectType( bContainer ? prop._elementTypeName : prop._typeName, bContainer ? prop.findElementBinding() : prop.findValueBinding(), context ) != nullptr;
                    const ISequenceContainerWrapper* pSequence    = bContainer && prop._containerWrapper != nullptr ? prop._containerWrapper->asSequence() : nullptr;
                    if ( bStructValue && bContainer == false && prop._bIsBitField == SW_FALSE )
                        return ReflectVisitFlow::Continue;
                    if ( bStructValue && pSequence != nullptr )
                    {
                        outListValue.push_back( DefaultValue{ path, field._pValue != nullptr ? static_cast<uint64>( pSequence->getSize( field._pValue ) ) : 0u } );
                        return ReflectVisitFlow::Continue;
                    }
                    bytes.clear();
                    if ( SerializerUtil::canCarryProperty( prop, context ) && SerializerUtil::writePropertyBinary( prop, ObjectAddress{ field._pObject }, bytes, context ) )
                        outListValue.push_back( DefaultValue{ path, HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false ) } );
                    return ReflectVisitFlow::SkipChildren;
                } );
            }

            /**
             * @brief 두 기본 인스턴스에서 **값이 같은** 칸을 접습니다. 만들 때마다 다른 값(GUID · 런타임 id · 시각)은 기본값이 아니라 빠진다 — 접으면 쿠킹마다 키가 달라진다.
             * @return 접을 칸이 없으면 0 입니다.
             */
            static uint64 foldCommonValues( const vector<DefaultValue>& listFirst, const vector<DefaultValue>& listSecond )
            {
                uint64       hash        = 0;
                const size_t commonCount = MathUtil::min( listFirst.size(), listSecond.size() );
                for ( size_t index = 0; index < commonCount; ++index )
                {
                    const bool bSame = listFirst[index]._path == listSecond[index]._path && listFirst[index]._value == listSecond[index]._value;
                    if ( bSame )
                        hash = HashUtil::combine( HashUtil::combine( hash, listFirst[index]._path ), listFirst[index]._value );
                }
                return hash;
            }

            /**
             * @brief @p type 의 기본 인스턴스가 든 저작 값의 해시입니다. 만들 수 없는 타입(추상 · 데이터로 놓을 수 없는 컴포넌트 · 기본 생성자가 없는 타입)이거나 저작 칸이 없으면 0 입니다 —
             *        데이터가 그 타입을 만들 일이 없어 기본값이 쿠킹본에 닿지 않으므로 배치 해시만으로 충분하다.
             * @param scratch 컴포넌트를 붙일 씬 밖 매니저입니다. 훅을 부르지 않는다(`GameObjectManager::TransientTag`) — 게임 설정의 `<Defaults>` 는 붙을 때 들어간다.
             */
            static uint64 computeDefaultHash( const TypeInfo& type, const TypeRegistry& registry, GameObjectManager& scratch )
            {
                bool bHasAuthored = false;
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    bHasAuthored = bHasAuthored || prop._metadata->isAuthored();
                }
                if ( bHasAuthored == false )
                    return 0;
                vector<DefaultValue> arrListValue[2];
                for ( vector<DefaultValue>& listValue : arrListValue )
                {
                    if ( type._addComponent != nullptr || &type == GameObject::StaticType() )
                    {
                        GameObject* const pOwner     = scratch.createGameObject( type._name );
                        const void* const pComponent = pOwner != nullptr && type._addComponent != nullptr ? type._addComponent( pOwner ) : nullptr;
                        const void* const pInstance  = type._addComponent != nullptr ? pComponent : pOwner;
                        if ( pInstance == nullptr )
                            return 0;
                        collectValues( type, pInstance, registry, listValue );
                        continue;
                    }
                    if ( type.isDerivedFrom( Component::StaticType() ) )
                        return 0; // 컴포넌트는 오브젝트에 붙여서만 만든다
                    // UI 위젯은 씬 · 프리팹 쿠킹본에 들지 않는다(UI 문서가 실행 때 만든다) — 기본값이 런타임 위젯 스킨(호스트 서비스)에서 오므로 헤드리스 쿠킹은 만들 수도 없다.
                    if ( type.isDerivedFrom( Widget::StaticType() ) )
                        return 0;
                    const ReflectedInstance instance = type.canConstruct() ? ReflectedInstance::create( type ) : ReflectedInstance{};
                    if ( instance.isValid() == false )
                        return 0;
                    collectValues( type, instance.get(), registry, listValue );
                }
                return foldCommonValues( arrListValue[0], arrListValue[1] );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint64 CookReflectionFingerprint::compute( const TypeRegistry& registry )
    {
        vector<const TypeInfo*> listType; // 기본 인스턴스를 만드는 일이 레지스트리를 다시 찾는다 — 순회 잠금 안에서 하지 않는다
        registry.forEachType( [&listType]( const TypeInfo& type )
        { listType.push_back( &type ); } );
        // 이름 차례로 만든다 — 만든 차례에서 나오는 값(런타임 id)이 실행마다 같은 자리에 선다.
        VectorUtil::sort( listType, []( const TypeInfo* pLeft, const TypeInfo* pRight )
        { return pLeft->_fullyQualifiedName.view() < pRight->_fullyQualifiedName.view(); } );

        GameObjectManager scratch{ GameObjectManager::TransientTag{} };
        vector<string>    listTypeText;
        listTypeText.reserve( listType.size() );
        for ( const TypeInfo* pType : listType )
        {
            StringBuilder<constant::buffer::kMax1024> textBuilder;
            textBuilder.appendFormat( "%#|layout=%#|default=%#", pType->_fullyQualifiedName.view(), pType->getSchemaLayoutHash(),
                                      CookReflectionFingerprintInternal::computeDefaultHash( *pType, registry, scratch ) );
            for ( const PropertyInfo& property : pType->_listProperty )
            {
                const PropertyMetadata& metadata   = *property._metadata;
                const uint32            mergeFlags = ( metadata._bRequired == SW_TRUE ? 1u : 0u ) | ( metadata._bInheritKey == SW_TRUE ? 2u : 0u ) | ( metadata._bAssetPath == SW_TRUE ? 4u : 0u );
                textBuilder.appendFormat( "|%#:%#:%#:%#", property._name.view(), mergeFlags, metadata._assetType, metadata._defaultValue );
                for ( const hashed_string& alias : property._listAlias )
                {
                    textBuilder.append( '~' ).append( alias.view() );
                }
            }
            listTypeText.push_back( string( textBuilder.view() ) );
        }
        registry.forEachEnum( [&listTypeText]( const EnumInfo& info )
        {
            vector<string>                           listEntry;
            StringBuilder<constant::buffer::kMax256> entryBuilder;
            for ( const auto& [name, value] : info._mapNameToValue )
            {
                entryBuilder.clear();
                entryBuilder.appendFormat( "%#=%#", name.toString(), value );
                listEntry.push_back( string( entryBuilder.view() ) );
            }
            VectorUtil::sort( listEntry );
            StringBuilder<constant::buffer::kMax1024> textBuilder;
            textBuilder.appendFormat( "enum %#", info._fullyQualifiedName.view() );
            for ( const string& entry : listEntry )
            {
                textBuilder.append( '|' ).append( entry );
            }
            listTypeText.push_back( string( textBuilder.view() ) );
        } );
        VectorUtil::sort( listTypeText );
        string canonical;
        for ( const string& text : listTypeText )
        {
            canonical.append( text ).append( "\n" );
        }
        return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
    }
} // namespace sw
