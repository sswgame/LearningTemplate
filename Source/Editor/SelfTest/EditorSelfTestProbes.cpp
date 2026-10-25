/**
 * @file EditorSelfTestProbes.cpp
 * @brief 입력 흉내 진입점(`EditorClick` · `EditorDrag` …)로 만든 결과를 시나리오가 읽을 에디터 탐침 몇 개입니다 — 열거형 프로퍼티 값, 오브젝트의 부모 깊이, 저장하지 않은 문서 수.
 * @details 값을 고르는 쪽(인스펙터 콤보, Hierarchy 끌어 놓기, 문서 패널 저장)이 바뀐 결과가 화면이 아니라 데이터에서 읽히게 한다. 대상은 `gv_editorProbeProperty` · `gv_editorProbeObject`
 *          (`EditorScenarioSteps.cpp` 가 정의하는 시나리오용 전역 변수)가 정한다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorProbeProperty );
    SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_editorProbeObject );

    namespace
    {
        struct EditorSelfTestProbesInternal
        {
            /**
             * @brief 주 선택 오브젝트의 `gv_editorProbeProperty`(`<컴포넌트 타입>.<프로퍼티>`)가 열거형이면 그 값(열거자의 수)입니다.
             * @details `Editor.SelectedProperty` 는 숫자 프로퍼티만 읽는다. 열거형은 크기와 부호가 제각각이라(`EnumInfo::_size`) 열거형 정보로 읽는다.
             */
            [[nodiscard]] static bool readSelectedEnumValue( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext  = editor::findSubsystem<EditorContext>();
                TypeRegistry*  pRegistry = editor::findSubsystem<TypeRegistry>();
                if ( pRegistry == nullptr ) // 필수 서비스: 없으면 버그
                {
                    SW_LOG_ERROR( "[Scenario] probe Editor.SelectedEnumValue: TypeRegistry is missing" );
                    return false;
                }
                const size_t dot                = gv_editorProbeProperty.find( '.' );
                const bool   bInvalidProbeSetup = pContext == nullptr || dot == string::npos;
                if ( bInvalidProbeSetup )
                    return false;
                GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject();
                if ( pObject == nullptr )
                    return false;
                Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorProbeProperty.substr( 0, dot ).c_str() ) );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                    return false;
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorProbeProperty.substr( dot + 1 ).c_str() ) );
                if ( pProperty == nullptr )
                    return false;
                const EnumInfo* pEnum = pRegistry->findEnum( pProperty->_typeName );
                if ( pEnum == nullptr )
                    return false;
                const void* pMemory = pProperty->getValuePtr<void>( pComponent );
                if ( pMemory == nullptr )
                    return false;
                outValue = static_cast<float64>( pEnum->readValueFromMemory( pMemory ) );
                return true;
            }

            /** @brief `gv_editorProbeObject` 오브젝트가 부모 몇 겹 아래 있는지입니다(루트 0). 그 이름의 오브젝트가 없으면 값을 내지 않습니다 — Hierarchy 끌어 놓기가 다시 부모를 정했는지 본다. */
            [[nodiscard]] static bool readObjectDepth( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GameObjectManager* pManager = editor::getActiveObjectManager();
                const GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( gv_editorProbeObject.c_str() ) ) : nullptr;
                if ( pObject == nullptr )
                    return false;
                uint32 depth = 0;
                for ( const GameObject* pParent = pObject->getParent(); pParent != nullptr; pParent = pParent->getParent() )
                {
                    ++depth;
                }
                outValue = static_cast<float64>( depth );
                return true;
            }

            /** @brief 저장하지 않은 편집이 있는 문서 패널 수입니다(`EditorPanelManager::countDirtyDocuments`). 저장 명령이 패널까지 닿았는지는 이 수가 줄어드는 것으로 본다. */
            [[nodiscard]] static bool readDirtyDocumentCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                outValue = static_cast<float64>( pContext->getSubsystem<EditorPanelManager>().countDirtyDocuments() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorSelectedEnumValue, "Editor.SelectedEnumValue",
                         "Enumerator value of gv_editorProbeProperty (<ComponentType>.<property>, an enum) on the primary selection", &EditorSelfTestProbesInternal::readSelectedEnumValue );
    SW_AUTOMATION_PROBE( editorObjectDepth, "Editor.ObjectDepth", "Parent levels above the object named by gv_editorProbeObject (0 = a root)", &EditorSelfTestProbesInternal::readObjectDepth );
    SW_AUTOMATION_PROBE( editorDirtyDocumentCount, "Editor.DirtyDocumentCount", "Document panels that hold edits not yet saved", &EditorSelfTestProbesInternal::readDirtyDocumentCount );
} // namespace sw::editor
