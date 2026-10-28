/**
 * @file ObjectWatchs.cpp
 * @brief 활성 씬의 오브젝트를 이름(`target`)으로 읽는 자동화 탐침입니다 — `Object.X` · `Object.Y` · `Object.Z` · `Object.Exists` · `Object.Property` · `Object.DistanceTo`.
 * @details 대상 고르기 규칙은 `AutomationTarget`(`Engine/Automation/Watch/AutomationWatch.h`)이 정한다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 질의 탐침의 대상 오브젝트 월드 위치입니다. 대상이 없으면 이유를 적고 false 입니다. */
        [[nodiscard]] bool readTargetPosition( const AutomationWatchQuery& query, float3& outPosition, AutomationWatchValue& outValue )
        {
            const GameObject*     pTarget = query.findTargetObject();
            const SceneComponent* pScene  = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
            {
                outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "transform" ) );
                return false;
            }
            outPosition = pScene->getWorldPosition();
            return true;
        }

        /** @brief 탐침 `Object.X` · `Object.Y` · `Object.Z` 의 몸통입니다. */
        [[nodiscard]] bool readObjectAxis( const AutomationWatchQuery& query, uint32 axis, AutomationWatchValue& outValue )
        {
            float3 position{};
            if ( readTargetPosition( query, position, outValue ) == false )
                return false;
            outValue.setNumber( static_cast<float64>( axis == 0 ? position._x : ( axis == 1 ? position._y : position._z ) ) );
            return true;
        }

        [[nodiscard]] bool readObjectX( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            return readObjectAxis( query, 0, outValue );
        }

        [[nodiscard]] bool readObjectY( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            return readObjectAxis( query, 1, outValue );
        }

        [[nodiscard]] bool readObjectZ( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            return readObjectAxis( query, 2, outValue );
        }

        /** @brief 탐침 `Object.Exists` 입니다. 활성 씬에 그 오브젝트가 있으면 1, 없으면 0 입니다. 고를 수 없는 대상(같은 이름 여럿, 틀린 번호 글)은 값이 없습니다. */
        [[nodiscard]] bool readObjectExists( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            if ( query._pManager == nullptr )
                return false;
            if ( query.getTargetLookup() == AutomationTargetLookup::Rejected )
            {
                outValue.setText( query.getTargetProblem() );
                return false;
            }
            outValue.setNumber( query.getTargetLookup() == AutomationTargetLookup::Found ? 1.0 : 0.0 );
            return true;
        }

        /**
         * @brief 탐침 `Object.Property` 입니다. 대상 오브젝트의 컴포넌트 PROPERTY 값(`key="<컴포넌트 타입>.<프로퍼티>"`)입니다.
         * @details 수(float32 · int32 · uint32)와 글(string)을 읽는다. 선택과 무관하게 이름으로 고르므로 여러 오브젝트를 한 프레임에 비교할 수 있다
         *          (프리팹 인스턴스마다의 값, 플레이 전후의 값). 비트필드 bool 은 PROPERTY 주소가 없어 읽지 못한다.
         */
        [[nodiscard]] bool readObjectProperty( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject* pTarget = query.findTargetObject();
            if ( pTarget == nullptr )
            {
                outValue.setText( query.getTargetProblem() );
                return false;
            }
            const size_t dot = query._key.find( '.' );
            if ( dot == string_view::npos )
            {
                outValue.setText( "key must be <ComponentType>.<property>" );
                return false;
            }
            const string componentName( query._key.substr( 0, dot ) );
            const string propertyName( query._key.substr( dot + 1 ) );
            Component*   pComponent = pTarget->findComponentByTypeName( hashed_string( componentName ) );
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
            {
                outValue.setText( "no " + componentName + " on the target" );
                return false;
            }
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( propertyName ) );
            if ( pProperty == nullptr )
            {
                outValue.setText( "no property " + propertyName );
                return false;
            }
            // 갈래는 내장 타입 표 한 곳(`ReflectValueUtil`)이 정한다 — 숫자 · bool 은 수, 글 갈래는 글로 읽는다.
            const int32               builtinIndex = ReflectValueUtil::findBuiltinIndex( pProperty->_typeName );
            const ReflectBuiltinClass builtinClass = ReflectValueUtil::getBuiltinClass( builtinIndex );
            const void* const         pValue       = pProperty->getRawPtr( static_cast<const void*>( pComponent ) );
            float64                   number{ 0.0 };
            int64                     integer{ 0 };
            // 비트필드는 주소가 없어(pValue == nullptr) 읽는 함수가 모두 false 다.
            if ( builtinClass == ReflectBuiltinClass::Number && ReflectValueUtil::readNumber( builtinIndex, pValue, number ) )
                outValue.setNumber( number );
            else if ( builtinClass == ReflectBuiltinClass::Bool && ReflectValueUtil::readInteger( builtinIndex, pValue, integer ) )
                outValue.setNumber( static_cast<float64>( integer ) );
            else if ( builtinClass == ReflectBuiltinClass::Text && pValue != nullptr )
                outValue.setText( ReflectValueUtil::formatBuiltin( builtinIndex, pValue ) );
            else
            {
                outValue.setText( "property type " + string( pProperty->_typeName.c_str() ) + " is not readable here" );
                return false;
            }
            return true;
        }

        /** @brief 탐침 `Object.DistanceTo` 입니다. 대상과 `key` 이름의 오브젝트 사이 월드 거리(m)입니다. */
        [[nodiscard]] bool readObjectDistanceTo( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            float3 position{};
            if ( readTargetPosition( query, position, outValue ) == false )
                return false;
            const AutomationWatchQuery otherQuery = AutomationWatchQuery::make( query._pManager, query._key, string_view{} );
            float3                     otherPosition{};
            if ( readTargetPosition( otherQuery, otherPosition, outValue ) == false )
                return false;
            outValue.setNumber( static_cast<float64>( float3::getDistance( position, otherPosition ) ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( objectX, "Object.X", "World X of the target object (m)", &readObjectX, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectY, "Object.Y", "World Y of the target object (m)", &readObjectY, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectZ, "Object.Z", "World Z of the target object (m)", &readObjectZ, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectExists, "Object.Exists", "1 when the active scene has an object named target", &readObjectExists, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectProperty, "Object.Property", "Component property of the target object (key = <ComponentType>.<property>; numbers and strings)",
                               &readObjectProperty, true, true );
    SW_AUTOMATION_QUERY_WATCH( objectDistanceTo, "Object.DistanceTo", "World distance from the target object to the object named key (m)", &readObjectDistanceTo, true, true );
} // namespace sw
