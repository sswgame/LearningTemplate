#include "pch.h"

#include "Editor/Panels/Inspector/Model/SceneObjectReferencePickerState.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectReference.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct SceneObjectReferencePickerStateInternal
        {
            static string formatValue( const SceneObjectReference& value, const PropertyInfo& prop )
            {
                // 참조는 값 자체라 값 주소 쪽 변환으로 쓴다(객체 주소 쪽은 오프셋을 한 번 더 더한다).
                return SerializerUtil::formatValueText( ValueAddress{ &value }, prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
            }

            /** @brief 해석된 번호의 오브젝트를 그 참조의 씬에서 찾습니다. */
            static const GameObject* findObject( const SceneManager& sceneManager, const SceneObjectReference& value, uint64 objectID )
            {
                for ( const unique_ptr<Scene>& scene : sceneManager.getLoadedScenes() )
                {
                    if ( scene == nullptr || scene->getObjectManager() == nullptr )
                        continue;
                    if ( SceneObjectReference::isSameScenePath( scene->getSourcePath(), value._scenePath ) )
                        return scene->getObjectManager()->findGameObjectByID( objectID );
                }
                return nullptr;
            }
        };
    } // namespace

    SceneObjectReferencePickerState::SceneObjectReferencePickerState()
        : _preview{}
        , _currentText{}
        , _noneText{}
        , _listScene{}
        , _bWarn{ SW_FALSE }
    {
    }

    bool SceneObjectReferencePickerState::gatherPreview( const PropertyGridModel& model, uint32 rowID, SceneObjectReferencePickerState& outChoices )
    {
        using Internal = SceneObjectReferencePickerStateInternal;
        outChoices._listScene.clear();
        MutableObjectAddress instance{ nullptr };
        const PropertyInfo*  pProp = nullptr;
        if ( model.findRowValue( rowID, instance, pProp ) == false )
            return false;
        const PropertyInfo&         prop   = *pProp;
        const SceneObjectReference* pValue = prop.getValuePtr<SceneObjectReference>( instance.get() );
        if ( pValue == nullptr )
            return false;
        const SceneManager& sceneManager = editor::getSubsystem<SceneManager>();

        uint64                                  resolvedID{ 0 };
        const SceneObjectResolveResult          state = SceneObjectReference::resolve( sceneManager, *pValue, resolvedID );
        fixed_string<constant::buffer::kMax512> preview;
        switch ( state )
        {
            case SceneObjectResolveResult::Unset:
            {
                formatstring( preview.data(), preview.capacity(), "(none)" );
                break;
            }
            case SceneObjectResolveResult::SceneNotLoaded:
            {
                formatstring( preview.data(), preview.capacity(), "(scene not loaded) %# #%#", pValue->_scenePath, pValue->_fileID );
                break;
            }
            case SceneObjectResolveResult::ObjectMissing:
            {
                formatstring( preview.data(), preview.capacity(), "(missing) %# #%#", pValue->_scenePath, pValue->_fileID );
                break;
            }
            case SceneObjectResolveResult::Resolved:
            {
                const GameObject* pObject = Internal::findObject( sceneManager, *pValue, resolvedID );
                formatstring( preview.data(), preview.capacity(), "%# / %#", FileUtil::getFileNamePart( pValue->_scenePath ), pObject != nullptr ? pObject->getName().c_str() : "?" );
                break;
            }
        }
        outChoices._preview     = preview.c_str();
        outChoices._currentText = Internal::formatValue( *pValue, prop );
        outChoices._noneText    = Internal::formatValue( SceneObjectReference{}, prop );
        outChoices._bWarn       = state == SceneObjectResolveResult::ObjectMissing ? SW_TRUE : SW_FALSE;
        return true;
    }

    void SceneObjectReferencePickerState::gatherScenes( const PropertyGridModel& model, uint32 rowID, SceneObjectReferencePickerState& inoutChoices )
    {
        using Internal = SceneObjectReferencePickerStateInternal;
        inoutChoices._listScene.clear();
        MutableObjectAddress instance{ nullptr };
        const PropertyInfo*  pProp = nullptr;
        if ( model.findRowValue( rowID, instance, pProp ) == false )
            return;
        const PropertyInfo&         prop   = *pProp;
        const SceneObjectReference* pValue = prop.getValuePtr<SceneObjectReference>( instance.get() );
        if ( pValue == nullptr )
            return;
        for ( const unique_ptr<Scene>& scene : editor::getSubsystem<SceneManager>().getLoadedScenes() )
        {
            if ( scene == nullptr || scene->getObjectManager() == nullptr )
                continue;
            inoutChoices._listScene.push_back( SceneObjectReferenceScene{} );
            SceneObjectReferenceScene& sceneRow = inoutChoices._listScene.back();
            const string&              path     = scene->getSourcePath();
            // 경로가 없는 씬(저장한 적 없음)은 참조를 만들 수 없다 — 제목을 비워 두면 그리는 쪽이 이유를 보인다.
            if ( path.empty() )
                continue;
            sceneRow._title = FileUtil::getFileNamePart( path ).c_str();
            // 콜백은 오브젝트 맵의 공유 잠금 안이라 이름과 번호만 모으고, 참조는 잠금 밖에서 만든다.
            vector<SceneObjectReferenceChoice> listCollected;
            scene->getObjectManager()->forEachGameObject( [&listCollected]( GameObject* pObject )
            { listCollected.push_back( SceneObjectReferenceChoice{ string( pObject->getName().c_str() ), string(), pObject->getObjectID() } ); } );
            VectorUtil::sort( listCollected, []( const SceneObjectReferenceChoice& lhs, const SceneObjectReferenceChoice& rhs )
            { return lhs._label < rhs._label; } );
            for ( SceneObjectReferenceChoice& choice : listCollected )
            {
                SceneObjectReference candidate{};
                if ( SceneObjectReference::makeReference( *scene, choice._objectID, candidate ) == false )
                    continue;
                choice._valueText = Internal::formatValue( candidate, prop );
                sceneRow._listChoice.push_back( std::move( choice ) );
            }
        }
    }
} // namespace sw::editor
