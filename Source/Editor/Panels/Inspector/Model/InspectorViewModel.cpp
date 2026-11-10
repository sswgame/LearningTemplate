#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Diagnostics/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorMultiSelection.h"
#include "Editor/Panels/Inspector/Model/InspectorObjectEdit.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    /** @brief 다중 선택의 공통 컴포넌트입니다. 명령 헤더의 형식이라 ViewModel 헤더에 두지 않는다. */
    struct InspectorViewModel::MultiEdit
    {
        vector<GameObject*>              _listObject; ///< 이번 gather 의 선택(주 선택이 앞)
        vector<EditorMultiEditComponent> _listCommon; ///< 공통 컴포넌트(gather 마다 다시 모은다)
    };

    namespace
    {
        struct InspectorViewModelInternal
        {
            static EditorWorkspace& getWorkspace()
            {
                return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
            }

            static Component* findTargetComponent( const EditorCommandTarget& target )
            {
                GameObject* pOwner = nullptr;
                return EditorSceneObjectCommands::findTargetComponent( target, pOwner );
            }

            static GameObject* findMainObject( uint64 objectID )
            {
                return objectID != 0 ? PropertyGridModel::findSceneObject( EditorPropertyGridScope::MainScene, objectID ) : nullptr;
            }

            /** @brief 오브젝트의 컴포넌트 카드를 채웁니다. 스크롤 요청은 그 컴포넌트 카드에 표시한다. */
            static void gatherComponentCards( const GameObject& object, uint64 scrollToComponentID, vector<InspectorComponentCard>& outListCard )
            {
                outListCard.clear();
                for ( Component* pComp : object.getComponents() )
                {
                    if ( pComp == nullptr )
                        continue;
                    InspectorComponentCard card;
                    card._componentID     = pComp->getComponentID();
                    card._title           = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str() : "Component";
                    const TypeInfo* pType = pComp->getTypeInfo();
                    card._typeName        = pType != nullptr ? pType->_name : hashed_string();
                    card._typeDisplayName = pType != nullptr ? pType->getDisplayName() : "";
                    card._bSelfActive     = pComp->isSelfActive() ? SW_TRUE : SW_FALSE;
                    card._bAccent         = isA<SceneComponent>( pComp ) ? SW_TRUE : SW_FALSE;
                    card._bScrollTo       = scrollToComponentID != 0 && scrollToComponentID == card._componentID ? SW_TRUE : SW_FALSE;
                    outListCard.push_back( std::move( card ) );
                }
            }
        };
    } // namespace

    InspectorComponentCard::InspectorComponentCard()
        : _componentID{ 0 }
        , _title{}
        , _typeName{}
        , _typeDisplayName{}
        , _bSelfActive{ SW_FALSE }
        , _bAccent{ SW_FALSE }
        , _bScrollTo{ SW_FALSE }
    {
    }

    InspectorDataLayerRow::InspectorDataLayerRow()
        : _name{}
        , _bOn{ SW_FALSE }
    {
    }

    InspectorViewState::InspectorViewState()
        : _content{ InspectorContent::NothingSelected }
        , _objectID{ 0 }
        , _lockedObjectID{ 0 }
        , _objectName{}
        , _parentName{}
        , _listTag{}
        , _prefabPath{}
        , _outerName{}
        , _outerPrefabPath{}
        , _instanceLabel{}
        , _listDataLayer{}
        , _listComponent{}
        , _listFavorite{}
        , _previewSceneName{}
        , _selectedCount{ 0 }
        , _unsharedCount{ 0 }
        , _bSceneEditsAllowed{ SW_TRUE }
        , _bActive{ SW_FALSE }
        , _bActiveMixed{ SW_FALSE }
        , _bHasParent{ SW_FALSE }
        , _bHasOuter{ SW_FALSE }
        , _bInstanceRelated{ SW_FALSE }
        , _bHasObjectType{ SW_FALSE }
    {
    }

    InspectorViewModel::InspectorViewModel()
        : _favorites{}
        , _pMultiEdit{ make_unique<MultiEdit>() }
        , _presetJob{}
        , _listPresetFile{}
        , _lockedObjectID{ 0 }
        , _shownObjectID{ 0 }
        , _currentMultiComponent{ kNoMultiComponent }
        , _shownScope{ EditorPropertyGridScope::None }
        , _bPresetDirty{ SW_TRUE }
        , _bFavoritesLoaded{ SW_FALSE }
    {
    }

    InspectorViewModel::~InspectorViewModel() = default;

    void InspectorViewModel::gather( InspectorViewState& outState )
    {
        using Internal = InspectorViewModelInternal;
        // 저장 파일은 처음 그릴 때 읽는다. 패널을 만들 때는 에디터 상태 폴더가 아직 정해지지 않았을 수 있다.
        if ( _bFavoritesLoaded == SW_FALSE )
        {
            (void)_favorites.loadFromFile( InspectorFavorites::getDefaultFilePath() ); // 없으면 빈 집합이다(처음 실행)
            _bFavoritesLoaded = SW_TRUE;
        }
        outState._lockedObjectID     = _lockedObjectID;
        outState._bSceneEditsAllowed = EditorPlaySession::areSceneEditsAllowed() ? SW_TRUE : SW_FALSE;
        outState._listComponent.clear();
        outState._listFavorite.clear();
        outState._listDataLayer.clear();
        outState._listTag.clear();
        _pMultiEdit->_listObject.clear();
        _pMultiEdit->_listCommon.clear();
        _shownObjectID = 0;
        _shownScope    = EditorPropertyGridScope::None;

        // 미리보기 창의 선택을 겨누면 그 창 씬의 오브젝트를 보인다. 잠금은 메인 선택의 오브젝트에만 쓴다.
        const EditorSelection* pFocusedSelection = editor::getFocusedSelection();
        if ( _lockedObjectID == 0 && pFocusedSelection != nullptr && pFocusedSelection->isPreviewScope() )
        {
            const Scene* pPreviewScene = pFocusedSelection->getScene();
            outState._previewSceneName = pPreviewScene != nullptr ? pPreviewScene->getName().c_str() : "?";
            GameObject* pObj           = pFocusedSelection->getPrimaryObject();
            if ( pObj == nullptr )
            {
                outState._content = InspectorContent::PreviewEmpty;
                return;
            }
            outState._content    = InspectorContent::Preview;
            outState._objectID   = pObj->getObjectID();
            outState._objectName = pObj->getName().c_str();
            _shownObjectID       = outState._objectID;
            _shownScope          = EditorPropertyGridScope::FocusedPreview;
            Internal::gatherComponentCards( *pObj, 0, outState._listComponent );
            return;
        }

        EditorWorkspace& workspace = Internal::getWorkspace();
        editor::getMainSceneSelection().getSelectedObjects( _pMultiEdit->_listObject );
        if ( _pMultiEdit->_listObject.size() > 1 && _lockedObjectID == 0 )
        {
            const vector<GameObject*>& listObject = _pMultiEdit->_listObject;
            GameObject*                pPrimary   = listObject[0];
            outState._content                     = InspectorContent::Multi;
            outState._selectedCount               = static_cast<uint32>( listObject.size() );
            outState._objectID                    = pPrimary->getObjectID();
            outState._objectName                  = pPrimary->getName().c_str();
            _shownObjectID                        = outState._objectID;
            _shownScope                           = EditorPropertyGridScope::MainScene;
            // 활성은 모든 선택에 적용한다. 값이 섞여 있으면 혼합으로 표시하고, 체크는 주 선택의 값을 따른다.
            const InspectorMultiSelection::ActiveState activeState = InspectorMultiSelection::readActiveState( listObject );
            outState._bActive                                      = activeState._bActive ? SW_TRUE : SW_FALSE;
            outState._bActiveMixed                                 = activeState._bMixed ? SW_TRUE : SW_FALSE;
            EditorMultiEditComponent::collectCommonComponents( listObject, _pMultiEdit->_listCommon );
            for ( const EditorMultiEditComponent& common : _pMultiEdit->_listCommon )
            {
                const Component*       pComponent = common._listComponent[0];
                InspectorComponentCard card;
                card._componentID     = pComponent->getComponentID();
                card._typeName        = pComponent->getTypeInfo()->_name;
                card._typeDisplayName = pComponent->getTypeInfo()->getDisplayName();
                outState._listComponent.push_back( std::move( card ) );
            }
            outState._unsharedCount = static_cast<uint32>( InspectorMultiSelection::countUnsharedComponents( *pPrimary, _pMultiEdit->_listCommon ) );
            // 다중 선택의 즐겨찾기는 주 선택 오브젝트 기준이다.
        }
        else
        {
            _pMultiEdit->_listObject.clear();
            const uint64 shownObjectID = _lockedObjectID != 0 ? _lockedObjectID : workspace.getSelectedObjectID();
            if ( shownObjectID == 0 )
            {
                outState._content = InspectorContent::NothingSelected;
                return;
            }
            Scene* pScene = editor::getActiveScene();
            if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            {
                outState._content = InspectorContent::NoActiveScene;
                return;
            }
            GameObject* pObj = pScene->getObjectManager()->findGameObjectByID( shownObjectID );
            if ( pObj == nullptr )
            {
                outState._content = InspectorContent::SelectionGone;
                if ( _lockedObjectID != 0 )
                    _lockedObjectID = 0; // 잠근 오브젝트가 사라졌으므로 선택을 따라가는 상태로 돌아간다
                else
                    workspace.clearSelection();
                return;
            }
            outState._content         = InspectorContent::Single;
            outState._objectID        = shownObjectID;
            outState._objectName      = pObj->getName().c_str();
            outState._bActive         = pObj->isActive() ? SW_TRUE : SW_FALSE;
            outState._bHasObjectType  = pObj->getTypeInfo() != nullptr ? SW_TRUE : SW_FALSE;
            const GameObject* pParent = pObj->getParent();
            outState._bHasParent      = pParent != nullptr ? SW_TRUE : SW_FALSE;
            outState._parentName      = pParent != nullptr ? pParent->getName().c_str() : "";
            for ( const TagID& tag : pObj->getTags().getTags() )
            {
                if ( StringUtil::isNullOrEmpty( tag._pString ) == false )
                    outState._listTag.push_back( string( tag._pString ) );
            }
            _shownObjectID = shownObjectID;
            _shownScope    = EditorPropertyGridScope::MainScene;

            // 프리팹 줄 — 중첩 부품이면 바깥 인스턴스의 프리팹에도 적용할 수 있다.
            outState._prefabPath      = string( workspace.findPrefabPath( pObj ) );
            const GameObject* pOuter  = outState._prefabPath.empty() ? nullptr : EditorInspectorCommands::findOuterInstance( pObj );
            outState._bHasOuter       = pOuter != nullptr ? SW_TRUE : SW_FALSE;
            outState._outerName       = pOuter != nullptr ? pOuter->getName().c_str() : "";
            outState._outerPrefabPath = pOuter != nullptr ? string( workspace.findPrefabPath( pOuter ) ) : string();

            // 씬 인스턴스 · 데이터 레이어 — 멤버의 레이어는 내부 씬의 엔티티와 앵커가 정하므로 여기서 고치지 않는다.
            const string_view instancePath   = pScene->getSceneInstancePath( shownObjectID );
            const uint64      anchorObjectID = pScene->findInstanceAnchorObjectID( shownObjectID );
            outState._bInstanceRelated       = instancePath.empty() == false || anchorObjectID != 0 ? SW_TRUE : SW_FALSE;
            const GameObject* pAnchor        = anchorObjectID != 0 ? pScene->getObjectManager()->findGameObjectByID( anchorObjectID ) : nullptr;
            outState._instanceLabel          = pAnchor != nullptr ? string( pAnchor->getName().c_str() ) : string( instancePath );
            if ( anchorObjectID == 0 )
            {
                const vector<string>* pEntityLayer = pScene->findEntityDataLayers( shownObjectID );
                for ( const SceneDataLayerDef& layer : pScene->getSceneSettings()._listDataLayer )
                {
                    InspectorDataLayerRow row;
                    row._name = layer._name;
                    row._bOn  = pEntityLayer != nullptr && VectorUtil::contains( *pEntityLayer, layer._name ) ? SW_TRUE : SW_FALSE;
                    outState._listDataLayer.push_back( std::move( row ) );
                }
            }

            Internal::gatherComponentCards( *pObj, workspace.getScrollToComponentID(), outState._listComponent );
        }

        // 즐겨찾기 구역 — 그릴 것이 있는 대상만 모은다(주 선택 오브젝트 기준).
        GameObject* pShown = Internal::findMainObject( _shownObjectID );
        if ( pShown == nullptr || _favorites.isEmpty() )
            return;
        if ( pShown->getTypeInfo() != nullptr && _favorites.hasAnyForTypeOrBase( *pShown->getTypeInfo() ) )
            outState._listFavorite.push_back( EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, _shownObjectID, 0 ) );
        for ( const Component* pComp : pShown->getComponents() )
        {
            const bool bFavoriteComponent = pComp != nullptr && pComp->getTypeInfo() != nullptr && _favorites.hasAnyForTypeOrBase( *pComp->getTypeInfo() );
            if ( bFavoriteComponent )
                outState._listFavorite.push_back( EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, _shownObjectID, pComp->getComponentID() ) );
        }
    }

    void InspectorViewModel::toggleLock()
    {
        // 보기 상태 — 되돌리기에 남기지 않는다.
        _lockedObjectID = _lockedObjectID != 0 ? 0 : InspectorViewModelInternal::getWorkspace().getSelectedObjectID();
    }

    void InspectorViewModel::consumeScrollTo()
    {
        // 보기 상태 — 되돌리기에 남기지 않는다.
        InspectorViewModelInternal::getWorkspace().setScrollToComponentID( 0 );
    }

    void InspectorViewModel::renameObject( uint64 objectID, string_view name )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj == nullptr || name.empty() )
            return;
        const hashed_string newName{ name };
        if ( pObj->getName().isEqual( newName, NameCase::CaseSensitive ) )
            return;
        InspectorObjectEdit::applyObjectEdit( pObj, "Rename GameObject", [pObj, newName]()
        { pObj->setName( newName ); } );
    }

    void InspectorViewModel::setObjectActive( uint64 objectID, bool bActive )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj != nullptr )
            EditorSceneCommands::setActive( pObj, bActive );
    }

    void InspectorViewModel::detachObjectFromParent( uint64 objectID )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj != nullptr )
            InspectorObjectEdit::applyObjectEdit( pObj, "Unparent GameObject", [pObj]()
            { pObj->detachFromParent(); } );
    }

    void InspectorViewModel::setComponentActive( uint64 objectID, uint64 componentID, bool bActive )
    {
        GameObject* pObj  = PropertyGridModel::findSceneObject( _shownScope, objectID );
        Component*  pComp = pObj != nullptr ? pObj->findComponentByID( componentID ) : nullptr;
        if ( pComp != nullptr )
            InspectorObjectEdit::applyObjectEdit( pObj, "Toggle Component Active", [pComp, bActive]()
            { pComp->setActive( bActive ); } );
    }

    void InspectorViewModel::removeComponent( uint64 objectID, uint64 componentID )
    {
        GameObject* pObj  = PropertyGridModel::findSceneObject( _shownScope, objectID );
        Component*  pComp = pObj != nullptr ? pObj->findComponentByID( componentID ) : nullptr;
        if ( pComp != nullptr )
            EditorSceneObjectCommands::removeComponent( pObj, pComp ); // 메뉴의 Remove Component 와 같은 한 곳
    }

    void InspectorViewModel::setMultiActive( bool bActive )
    {
        if ( _pMultiEdit->_listObject.empty() == false )
            InspectorMultiSelection::applyActive( _pMultiEdit->_listObject, bActive );
    }

    void InspectorViewModel::applyToPrefab( uint64 objectID )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj != nullptr )
            (void)EditorInspectorCommands::applyToPrefab( pObj, AssetID{ InspectorViewModelInternal::getWorkspace().findPrefabPath( pObj ) } ); // 실패는 프리팹 저장이 알린다
    }

    void InspectorViewModel::revertToPrefab( uint64 objectID )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj == nullptr )
            return;
        const string_view prefabPath = InspectorViewModelInternal::getWorkspace().findPrefabPath( pObj );
        if ( EditorInspectorCommands::revertToPrefab( pObj, prefabPath ) == false )
            SW_LOG_WARNING_ID( "InspectorPanel.RevertPrefabFailed", "Revert to Prefab failed for '%#' (%#)", pObj->getName().c_str(), prefabPath );
    }

    void InspectorViewModel::unlinkPrefab( uint64 objectID )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj != nullptr )
            (void)EditorInspectorCommands::unlinkPrefab( pObj ); // 거절 이유는 명령이 경고 로그로 남긴다. 부품이면 맨 바깥 인스턴스를 한 겹 푼다
    }

    void InspectorViewModel::applyToOuterPrefab( uint64 objectID )
    {
        GameObject* pObj = InspectorViewModelInternal::findMainObject( objectID );
        if ( pObj != nullptr )
            (void)EditorInspectorCommands::applyToOuterPrefab( pObj ); // 실패는 프리팹 저장이 알린다
    }

    void InspectorViewModel::applyInstanceOverrides()
    {
        // 인스턴스의 차이는 앵커의 `<Part>` 로 저장된다 — 내부 씬 파일은 그대로다.
        (void)editor::getSubsystem<SceneManager>().saveActiveScene(); // 실패는 저장이 알린다
    }

    void InspectorViewModel::setDataLayer( uint64 objectID, string_view layerName, bool bOn )
    {
        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr )
            return;
        vector<string>        listLayer;
        const vector<string>* pEntityLayer = pScene->findEntityDataLayers( objectID );
        if ( pEntityLayer != nullptr )
        {
            for ( const string& entityLayer : *pEntityLayer )
            {
                if ( entityLayer != layerName )
                    listLayer.push_back( entityLayer );
            }
        }
        if ( bOn )
            listLayer.push_back( string( layerName ) );
        (void)pScene->setEntityDataLayers( objectID, listLayer ); // 앵커 멤버는 gather 가 줄을 비워 여기 오지 않는다
    }

    void InspectorViewModel::copyEditToOthers( const PropertyInfo& prop )
    {
        if ( _currentMultiComponent < _pMultiEdit->_listCommon.size() )
            (void)EditorMultiEditComponent::copyPropertyToOthers( prop, _pMultiEdit->_listCommon[_currentMultiComponent]._listComponent ); // 수는 쓰지 않는다 — 되돌리기는 위젯 추적이 모두를 담는다
    }

    bool InspectorViewModel::isMultiEditMixed( const PropertyInfo& prop )
    {
        return _currentMultiComponent < _pMultiEdit->_listCommon.size() &&
               InspectorMultiSelection::hasMixedValues( prop, _pMultiEdit->_listCommon[_currentMultiComponent]._listComponent );
    }

    void InspectorViewModel::gatherPresets( const EditorCommandTarget& target, vector<InspectorPresetRow>& outListPreset )
    {
        outListPreset.clear();
        vector<string>       listNewPresetFile;
        const EditorTaskPoll presetPoll = _presetJob.take( listNewPresetFile );
        if ( presetPoll == EditorTaskPoll::Ready )
        {
            _listPresetFile = std::move( listNewPresetFile );
            _bPresetDirty   = SW_FALSE;
        }
        else if ( _bPresetDirty == SW_TRUE && presetPoll == EditorTaskPoll::Idle )
        {
            _presetJob.request( EditorGlobalVariableCommands::getComponentPresetFolderPath(), ".preset.xml", false );
        }
        Component* pComp = InspectorViewModelInternal::findTargetComponent( target );
        if ( pComp == nullptr )
            return;
        for ( const string& presetFile : _listPresetFile )
        {
            string displayName = EditorTransformCommands::getComponentPresetName( pComp, presetFile );
            if ( displayName.empty() == false )
                outListPreset.push_back( InspectorPresetRow{ presetFile, std::move( displayName ) } );
        }
    }

    bool InspectorViewModel::saveComponentPreset( const EditorCommandTarget& target, string_view presetName )
    {
        Component* pComp = InspectorViewModelInternal::findTargetComponent( target );
        if ( pComp == nullptr || EditorTransformCommands::saveComponentPreset( pComp, string( presetName ).c_str() ) == false )
        {
            SW_LOG_ERROR_ID( "InspectorPanel.CouldNotSaveComponent", "Could not save component preset '%#'", presetName );
            return false;
        }
        _bPresetDirty = SW_TRUE;
        return true;
    }

    void InspectorViewModel::loadComponentPreset( const EditorCommandTarget& target, string_view presetFile )
    {
        Component* pComp = InspectorViewModelInternal::findTargetComponent( target );
        if ( pComp == nullptr || EditorTransformCommands::loadComponentPreset( pComp, string( presetFile ) ) == false )
            SW_LOG_ERROR_ID( "InspectorPanel.CouldNotApplyComponent", "Could not apply component preset '%#'", presetFile );
    }

    bool InspectorViewModel::hasTargetComponent( const EditorCommandTarget& target )
    {
        return InspectorViewModelInternal::findTargetComponent( target ) != nullptr;
    }

    const vector<IInspectorComponent*>& InspectorViewModel::getInspectorsForType( const hashed_string& typeName )
    {
        static const vector<IInspectorComponent*> kEmpty{};
        const TypeInfo*                           pType = editor::getSubsystem<TypeRegistry>().findType( typeName );
        if ( pType == nullptr )
            return kEmpty;
        return editor::getSubsystem<EditorContext>().getSubsystem<InspectorComponentManager>().getInspectorsForType( *pType );
    }

    GameObject* InspectorViewModel::findShownObject() const
    {
        return PropertyGridModel::findSceneObject( _shownScope, _shownObjectID );
    }

    Component* InspectorViewModel::findShownComponent( uint64 componentID ) const
    {
        GameObject* pObj = findShownObject();
        return pObj != nullptr ? pObj->findComponentByID( componentID ) : nullptr;
    }
} // namespace sw::editor
