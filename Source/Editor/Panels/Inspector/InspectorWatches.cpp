/**
 * @file InspectorWatchs.cpp
 * @brief 인스펙터가 고치는 주 선택 오브젝트의 프로퍼티 · 컴포넌트 감시 값입니다 — `Editor.SelectedProperty*` · `Editor.SelectedCurveKeyCount` · `Editor.SelectedHandle` · `Editor.SelectedSceneReference` · `Editor.SelectedComponentCount`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentPicker.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Curve/FloatCurve.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectReference.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 주 선택 오브젝트의 `-gv_editorWatchProperty`(`<컴포넌트 타입>.<프로퍼티>`) 값을 읽습니다. 숫자 프로퍼티(float32, int32, uint32)만 읽습니다.
         * @details 선택이 없거나 그 컴포넌트나 프로퍼티가 없으면 값을 내지 않는다.
         */
        [[nodiscard]] bool readSelectedProperty( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            GameObject*  pObject = editor::getMainSceneSelection().getPrimaryObject();
            const size_t dot     = gv_editorWatchProperty.find( '.' );
            if ( pObject == nullptr || dot == string::npos )
                return false;
            Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorWatchProperty.substr( 0, dot ).c_str() ) );
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                return false;
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorWatchProperty.substr( dot + 1 ).c_str() ) );
            if ( pProperty == nullptr )
                return false;
            if ( pProperty->_typeName == hashed_string( "float32" ) )
                outValue = static_cast<float64>( *pProperty->getValuePtr<float32>( pComponent ) );
            else if ( pProperty->_typeName == hashed_string( "int32" ) )
                outValue = static_cast<float64>( *pProperty->getValuePtr<int32>( pComponent ) );
            else if ( pProperty->_typeName == hashed_string( "uint32" ) )
                outValue = static_cast<float64>( *pProperty->getValuePtr<uint32>( pComponent ) );
            else
                return false;
            return true;
        }

        /** @brief 선택한 오브젝트 모두의 `-gv_editorWatchProperty` 값의 합입니다. 다중 편집이 모든 오브젝트에 적용되었는지 확인하는 데 씁니다. 숫자 프로퍼티만 읽습니다. */
        [[nodiscard]] bool readSelectedPropertySum( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            const size_t   dot      = gv_editorWatchProperty.find( '.' );
            if ( pContext == nullptr || dot == string::npos )
                return false;
            vector<GameObject*> listSelected;
            editor::getMainSceneSelection().getSelectedObjects( listSelected );
            const hashed_string typeName( gv_editorWatchProperty.substr( 0, dot ).c_str() );
            const hashed_string propertyName( gv_editorWatchProperty.substr( dot + 1 ).c_str() );
            float64             sum{ 0.0 };
            for ( GameObject* pObject : listSelected )
            {
                Component* pComponent = pObject->findComponentByTypeName( typeName );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                    return false;
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( propertyName );
                if ( pProperty == nullptr )
                    return false;
                if ( pProperty->_typeName == hashed_string( "float32" ) )
                    sum += static_cast<float64>( *pProperty->getValuePtr<float32>( pComponent ) );
                else if ( pProperty->_typeName == hashed_string( "int32" ) )
                    sum += static_cast<float64>( *pProperty->getValuePtr<int32>( pComponent ) );
                else
                    return false;
            }
            outValue = sum;
            return listSelected.empty() == false;
        }

        /** @brief 주 선택의 `-gv_editorWatchProperty` 커브(`FloatCurve`)의 키 수입니다. 커브가 아니면 값을 내지 않습니다. */
        [[nodiscard]] bool readSelectedCurveKeyCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            const size_t   dot      = gv_editorWatchProperty.find( '.' );
            if ( pContext == nullptr || dot == string::npos )
                return false;
            GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject();
            if ( pObject == nullptr )
                return false;
            Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorWatchProperty.substr( 0, dot ).c_str() ) );
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                return false;
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorWatchProperty.substr( dot + 1 ).c_str() ) );
            if ( pProperty == nullptr || pProperty->_typeName != hashed_string( "FloatCurve" ) )
                return false;
            outValue = static_cast<float64>( pProperty->getValuePtr<FloatCurve>( pComponent )->_listKey.size() );
            return true;
        }

        /** @brief 주 선택 오브젝트의 컴포넌트 수입니다. */
        [[nodiscard]] bool readSelectedComponentCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            GameObject*    pObject  = pContext != nullptr ? editor::getMainSceneSelection().getPrimaryObject() : nullptr;
            if ( pObject == nullptr )
                return false;
            outValue = static_cast<float64>( pObject->getComponentCount() );
            return true;
        }

        /**
         * @brief 주 선택 오브젝트의 `-gv_editorWatchProperty`(`<컴포넌트 타입>.<프로퍼티>`) 컴포넌트 핸들이 가리키는 컴포넌트를 선택기와 같은 글(`<오브젝트> / <키>`)로 읽습니다.
         * @details 비어 있으면 `(none)` 입니다. 선택 · 컴포넌트 · 프로퍼티가 없거나 핸들 프로퍼티가 아니면 값을 내지 않는다.
         */
        [[nodiscard]] bool readSelectedHandle( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            GameObject*  pObject   = editor::getMainSceneSelection().getPrimaryObject();
            const size_t dot       = gv_editorWatchProperty.find( '.' );
            const bool   bNoObject = pObject == nullptr || pObject->getManager() == nullptr;
            if ( bNoObject || dot == string::npos )
                return false;
            Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorWatchProperty.substr( 0, dot ).c_str() ) );
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                return false;
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorWatchProperty.substr( dot + 1 ).c_str() ) );
            if ( pProperty == nullptr || pProperty->_handleTargetTypeName.empty() )
                return false;
            const ComponentHandle* pHandle = pProperty->getValuePtr<ComponentHandle>( pComponent );
            if ( pHandle == nullptr )
                return false;
            const Component* pTarget = pHandle->isValid() ? pObject->getManager()->resolveComponent( *pHandle ) : nullptr;
            outValue.setText( pTarget != nullptr ? InspectorComponentPickerUtil::makeLabel( *pTarget ) : string{ "(none)" } );
            return true;
        }

        /**
         * @brief 주 선택 오브젝트의 `-gv_editorWatchProperty`(`<컴포넌트 타입>.<프로퍼티>`) 씬 오브젝트 참조가 가리키는 오브젝트를 `<씬 파일> / <오브젝트 이름>` 으로 읽습니다.
         * @details 비어 있으면 `(none)`, 대상 씬이 올라와 있지 않으면 `(scene not loaded)`, 씬은 있는데 오브젝트가 없으면 `(missing)` 입니다.
         *          선택 · 컴포넌트 · 프로퍼티가 없거나 참조 프로퍼티가 아니면 값을 내지 않는다.
         */
        [[nodiscard]] bool readSelectedSceneReference( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            GameObject*  pObject = editor::getMainSceneSelection().getPrimaryObject();
            const size_t dot     = gv_editorWatchProperty.find( '.' );
            if ( pObject == nullptr || dot == string::npos )
                return false;
            Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorWatchProperty.substr( 0, dot ).c_str() ) );
            if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                return false;
            const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorWatchProperty.substr( dot + 1 ).c_str() ) );
            if ( pProperty == nullptr || SceneObjectReferenceUtil::isReferenceTypeName( pProperty->_typeName ) == false )
                return false;
            const SceneObjectReference* pReference    = pProperty->getValuePtr<SceneObjectReference>( pComponent );
            const SceneManager*         pSceneManager = editor::findSubsystem<SceneManager>();
            if ( pSceneManager == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] scene reference watch: SceneManager is missing" );
                return false;
            }
            if ( pReference == nullptr )
                return false; // 프로퍼티 값 포인터가 없으면 값이 없다
            uint64                         objectID{ 0 };
            const SceneObjectResolveResult state = SceneObjectReferenceUtil::resolve( *pSceneManager, *pReference, objectID );
            switch ( state )
            {
                case SceneObjectResolveResult::Unset:
                {
                    outValue.setText( "(none)" );
                    return true;
                }
                case SceneObjectResolveResult::SceneNotLoaded:
                {
                    outValue.setText( "(scene not loaded)" );
                    return true;
                }
                case SceneObjectResolveResult::ObjectMissing:
                {
                    outValue.setText( "(missing)" );
                    return true;
                }
                case SceneObjectResolveResult::Resolved:
                {
                    break;
                }
            }
            for ( const unique_ptr<Scene>& scene : pSceneManager->getLoadedScenes() )
            {
                if ( scene == nullptr || scene->getObjectManager() == nullptr )
                    continue;
                if ( SceneObjectReferenceUtil::isSameScenePath( scene->getSourcePath(), pReference->_scenePath ) == false )
                    continue;
                const GameObject* pTarget = scene->getObjectManager()->findGameObjectByID( objectID );
                if ( pTarget == nullptr )
                    break;
                string text = FileUtil::getFileNamePart( pReference->_scenePath );
                text += " / ";
                text += pTarget->getName().c_str();
                outValue.setText( text );
                return true;
            }
            outValue.setText( "(missing)" );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( editorSelectedHandle, "Editor.SelectedHandle",
                               "Component the typed handle property named by gv_editorWatchProperty points at, as '<object> / <key>' ('(none)' when empty)",
                               &readSelectedHandle, false, false );
    SW_AUTOMATION_QUERY_WATCH( editorSelectedSceneReference, "Editor.SelectedSceneReference",
                               "Object the scene object reference property named by gv_editorWatchProperty points at, as '<scene file> / <object>' ('(none)' when empty)",
                               &readSelectedSceneReference, false, false );
    SW_AUTOMATION_WATCH( editorSelectedProperty, "Editor.SelectedProperty",
                         "Numeric value of gv_editorWatchProperty (<ComponentType>.<property>) on the primary selection", &readSelectedProperty );
    SW_AUTOMATION_WATCH( editorSelectedPropertySum, "Editor.SelectedPropertySum",
                         "Sum of gv_editorWatchProperty (<ComponentType>.<property>) over every selected object", &readSelectedPropertySum );
    SW_AUTOMATION_WATCH( editorSelectedCurveKeyCount, "Editor.SelectedCurveKeyCount",
                         "Keys of the FloatCurve gv_editorWatchProperty (<ComponentType>.<property>) on the primary selection",
                         &readSelectedCurveKeyCount );
    SW_AUTOMATION_WATCH( editorSelectedComponentCount, "Editor.SelectedComponentCount", "Components on the primary selection",
                         &readSelectedComponentCount );
} // namespace sw::editor
