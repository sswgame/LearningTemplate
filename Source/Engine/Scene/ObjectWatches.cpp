/**
 * @file ObjectWatches.cpp
 * @brief 활성 씬의 오브젝트를 이름(`target`)으로 읽는 자동화 감시 값입니다 — `Object.X` · `Object.Y` · `Object.Z` · `Object.Exists` · `Object.Property` · `Object.DistanceTo` · `Camera.FrameObject` · `Camera.DrawnAspect` · `Camera.ObjectInFrustum` · `Camera.ObjectScreenSize`.
 * @details 대상 고르기 규칙은 `AutomationTarget`(`Engine/Automation/Watch/AutomationWatch.h`)이 정한다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/View/SceneViewQuery.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

namespace sw
{
    namespace
    {
        /** @brief 질의 감시 값의 대상 오브젝트 월드 위치입니다. 대상이 없으면 이유를 적고 false 입니다. */
        [[nodiscard]] bool readTargetPosition( const AutomationWatchQuery& query, float3& outPosition, AutomationWatchValue& outValue )
        {
            const GameObject*     pTarget = query.findTargetObject();
            const SceneComponent* pScene  = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
            if ( pScene == nullptr )
            {
                outValue.setText( query.describeMissing( "transform" ) );
                return false;
            }
            outPosition = pScene->getWorldPosition();
            return true;
        }

        /** @brief 감시 값 `Object.X` · `Object.Y` · `Object.Z` 의 몸통입니다. */
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

        /** @brief 감시 값 `Object.Exists` 입니다. 활성 씬에 그 오브젝트가 있으면 1, 없으면 0 입니다. 고를 수 없는 대상(같은 이름 여럿, 틀린 번호 글)은 값이 없습니다. */
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
         * @brief 감시 값 `Object.Property` 입니다. 대상 오브젝트의 컴포넌트 PROPERTY 값(`key="<컴포넌트 타입>.<프로퍼티>"`)입니다.
         * @details 수(float32 · int32 · uint32)와 글(string)을 읽는다. 선택과 무관하게 이름으로 고르므로 여러 오브젝트를 한 프레임에 비교할 수 있다
         *          (프리팹 인스턴스마다의 값, 플레이 전후의 값). 카탈로그 키(`CatalogKey`)는 항목 id 글로 읽는다. 비트필드 bool 은 PROPERTY 주소가 없어 읽지 못한다.
         */
        [[nodiscard]] bool readObjectProperty( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const GameObject* pTarget = query.findTargetObject( outValue );
            if ( pTarget == nullptr )
                return false;
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
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "no %# on the target", componentName );
                outValue.setText( text );
                return false;
            }
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( propertyName ) );
            if ( pProperty == nullptr )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "no property %#", propertyName );
                outValue.setText( text );
                return false;
            }
            // 갈래는 내장 타입 표 한 곳(`ReflectedValueUtil`)이 정한다 — 숫자 · bool 은 수, 글 갈래는 글로 읽는다.
            // Other 가 아닌 갈래는 내장 타입에서만 나오므로 아래 갈래 안에서는 자리가 있다.
            const ReflectBuiltinIndex builtinIndex = pProperty->resolveValueFacts( engine::getSubsystem<TypeRegistry>() )._builtinIndex;
            const ReflectBuiltinClass builtinClass = ReflectedValueUtil::getBuiltinClass( builtinIndex );
            const void* const         pValue       = pProperty->getRawPtr( static_cast<const void*>( pComponent ) );
            float64                   number{ 0.0 };
            int64                     integer{ 0 };
            // 비트필드는 주소가 없어(pValue == nullptr) 읽는 함수가 모두 false 다.
            if ( builtinClass == ReflectBuiltinClass::Number && ReflectedValueUtil::readNumber( builtinIndex, pValue, number ) )
                outValue.setNumber( number );
            else if ( builtinClass == ReflectBuiltinClass::Bool && ReflectedValueUtil::readInteger( builtinIndex, pValue, integer ) )
                outValue.setNumber( static_cast<float64>( integer ) );
            else if ( builtinClass == ReflectBuiltinClass::Text && pValue != nullptr )
                outValue.setText( ReflectedValueUtil::formatBuiltin( builtinIndex, pValue ) );
            else if ( pProperty->_typeName == hashed_string( CatalogKeyBase::kTypeName ) && pValue != nullptr )
                outValue.setText( static_cast<const CatalogKeyBase*>( pValue )->toText() ); // 카탈로그 키는 항목 id 글이다(인스펙터 · XML 과 같은 글)
            else
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "property type %# is not readable here", pProperty->_typeName.c_str() );
                outValue.setText( text );
                return false;
            }
            return true;
        }

        /** @brief 감시 값 `Object.DistanceTo` 입니다. 대상과 `key` 이름의 오브젝트 사이 월드 거리(m)입니다. */
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

        /** @brief 감시 값 `Camera.FrameObject` 입니다. 이번 프레임에 화면을 그리는 게임 카메라의 오브젝트 이름입니다(시퀀서 컷이 덮으면 컷 카메라). 카메라가 없으면 값이 없습니다. */
        [[nodiscard]] bool readFrameCameraObject( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const CameraComponent* pCamera = query._pManager != nullptr ? query._pManager->getFrameCamera() : nullptr;
            const GameObject*      pOwner  = pCamera != nullptr ? pCamera->getOwner() : nullptr;
            if ( pOwner == nullptr )
            {
                outValue.setText( "no game camera this frame" );
                return false;
            }
            outValue.setText( pOwner->getName().c_str() );
            return true;
        }
        /** @brief 감시 값 `Camera.DrawnAspect` 입니다. 대상 오브젝트의 카메라가 마지막으로 그린 뷰의 가로세로 비이고, 그린 적이 없으면 0 입니다. */
        [[nodiscard]] bool readCameraDrawnAspect( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const CameraComponent* pCamera = query.findTargetComponent<CameraComponent>( "CameraComponent", outValue );
            if ( pCamera == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( pCamera->getDrawnAspect() ) );
            return true;
        }

        /**
         * @brief 이번 프레임에 화면을 그리는 카메라(`Camera.FrameObject` 와 같은 카메라)로 대상 오브젝트의 렌더 경계를 묻는 감시 값들의 몸통입니다.
         * @details 카메라가 그린 가로세로 비(`getDrawnAspect`)로 묻는다 — 아직 그린 적이 없으면 화면이 없으니 값이 없다(가짜 비율로 답하지 않는다).
         */
        template <typename Fn>
        [[nodiscard]] bool readFrameCameraObjectQuery( const AutomationWatchQuery& query, AutomationWatchValue& outValue, Fn&& readValue )
        {
            const CameraComponent* pCamera = query._pManager != nullptr ? query._pManager->getFrameCamera() : nullptr;
            if ( pCamera == nullptr || pCamera->getDrawnAspect() <= 0.0f )
            {
                outValue.setText( "no drawn game camera this frame" );
                return false;
            }
            const GameObject* pTarget = query.findTargetObject( outValue );
            if ( pTarget == nullptr )
                return false;
            outValue.setNumber( static_cast<float64>( readValue( pCamera->makeViewQuery( pCamera->getDrawnAspect() ), *pTarget ) ) );
            return true;
        }

        /** @brief 감시 값 `Camera.ObjectInFrustum` 입니다. 대상의 렌더 경계가 화면 카메라의 절두체에 걸치면 1, 아니면(렌더 경계가 없어도) 0 입니다. 가림은 보지 않습니다. */
        [[nodiscard]] bool readCameraObjectInFrustum( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            return readFrameCameraObjectQuery( query, outValue, []( const SceneViewQuery& view, const GameObject& object )
            { return CameraComponent::isObjectInFrustum( view, object ) ? 1.0f : 0.0f; } );
        }

        /** @brief 감시 값 `Camera.ObjectScreenSize` 입니다. 대상 렌더 경계의 화면 크기(지름 / 화면 높이, LOD 와 같은 식)이고 절두체 밖이면 0 입니다. */
        [[nodiscard]] bool readCameraObjectScreenSize( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            return readFrameCameraObjectQuery( query, outValue, []( const SceneViewQuery& view, const GameObject& object )
            { return CameraComponent::computeObjectScreenSize( view, object ); } );
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( objectX, "Object.X", "World X of the target object (m)", &readObjectX, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectY, "Object.Y", "World Y of the target object (m)", &readObjectY, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectZ, "Object.Z", "World Z of the target object (m)", &readObjectZ, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectExists, "Object.Exists", "1 when the active scene has an object named target", &readObjectExists, true, false );
    SW_AUTOMATION_QUERY_WATCH( objectProperty, "Object.Property", "Component property of the target object (key = <ComponentType>.<property>; numbers and strings)",
                               &readObjectProperty, true, true );
    SW_AUTOMATION_QUERY_WATCH( objectDistanceTo, "Object.DistanceTo", "World distance from the target object to the object named key (m)", &readObjectDistanceTo, true, true );
    SW_AUTOMATION_QUERY_WATCH( cameraDrawnAspect, "Camera.DrawnAspect", "Width over height of the view the target object's camera drew last (0 = never drawn)", &readCameraDrawnAspect, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraObjectInFrustum, "Camera.ObjectInFrustum",
                               "1 when the target object's render bounds touch the frustum of the camera drawing this frame (geometry only - occlusion is not checked)",
                               &readCameraObjectInFrustum, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraObjectScreenSize, "Camera.ObjectScreenSize",
                               "On-screen size (bounds diameter / screen height, as LOD uses) of the target object for the camera drawing this frame; 0 outside the frustum",
                               &readCameraObjectScreenSize, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraFrameObject, "Camera.FrameObject", "Name of the object whose camera draws this frame (a sequencer cut camera while one covers the frame)", &readFrameCameraObject, false, false );
} // namespace sw
