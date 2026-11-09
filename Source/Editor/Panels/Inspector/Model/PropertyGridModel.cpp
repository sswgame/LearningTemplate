#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/Model/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"
#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"
#include "Editor/Panels/Inspector/Model/InspectorObjectEdit.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyGridModelInternal
        {
            /** @brief 대상 범위의 씬입니다. 미리보기 범위는 지금 겨눈 미리보기 선택의 씬이고, 겨눈 창이 미리보기가 아니면 없습니다. */
            static Scene* findScene( EditorPropertyGridScope scope )
            {
                if ( scope == EditorPropertyGridScope::MainScene )
                    return editor::getActiveScene();
                const EditorSelection* pSelection = editor::getFocusedSelection();
                return pSelection != nullptr && pSelection->isPreviewScope() ? pSelection->getScene() : nullptr;
            }

            static GameObject* findObject( EditorPropertyGridScope scope, uint64 objectID )
            {
                Scene* pScene = findScene( scope );
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return nullptr;
                return pScene->getObjectManager()->findGameObjectByID( objectID );
            }

            /** @brief 편집 하나입니다. 주인 오브젝트가 있으면 되돌리기에 남기고(`InspectorObjectEdit`), 없으면(씬 밖 객체) 그냥 바꿉니다. */
            template <typename EditFunc>
            static void applyEdit( GameObject* pOwner, string_view undoLabel, EditFunc&& edit )
            {
                if ( pOwner == nullptr )
                    edit();
                else
                    InspectorObjectEdit::applyObjectEdit( pOwner, undoLabel, std::forward<EditFunc>( edit ) );
            }
        };
    } // namespace

    EditorPropertyGridTarget EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID )
    {
        EditorPropertyGridTarget target{};
        target._objectID    = objectID;
        target._componentID = componentID;
        target._scope       = scope;
        return target;
    }

    PropertyGridModel::PropertyGridModel()
        : _pComponent{ nullptr }
        , _pObject{ nullptr }
        , _pInstance{ nullptr }
        , _pType{ nullptr }
        , _onEdited{}
        , _isMixed{}
        , _pFavorites{ nullptr }
        , _layoutCache{}
        , _scratchDisplay{}
    {
    }

    bool PropertyGridModel::beginTarget( const EditorPropertyGridTarget& target )
    {
        endTarget();
        if ( target._scope == EditorPropertyGridScope::None )
        {
            if ( target._pInstance == nullptr || target._pType == nullptr )
                return false;
            _pInstance = target._pInstance;
            _pType     = target._pType;
        }
        else
        {
            GameObject* pObject = PropertyGridModelInternal::findObject( target._scope, target._objectID );
            if ( pObject == nullptr )
                return false;
            Component* pComponent = target._componentID != 0 ? pObject->findComponentByID( target._componentID ) : nullptr;
            if ( target._componentID != 0 && pComponent == nullptr )
                return false;
            const TypeInfo* pType = pComponent != nullptr ? pComponent->getTypeInfo() : pObject->getTypeInfo();
            if ( pType == nullptr )
                return false;
            _pObject    = pObject;
            _pComponent = pComponent;
            _pInstance  = pComponent != nullptr ? static_cast<void*>( pComponent ) : static_cast<void*>( pObject );
            _pType      = pType;
        }
        _onEdited = target._onEdited;
        _isMixed  = target._isMixed;
        return true;
    }

    void PropertyGridModel::endTarget()
    {
        _pComponent = nullptr;
        _pObject    = nullptr;
        _pInstance  = nullptr;
        _pType      = nullptr;
        _onEdited   = {};
        _isMixed    = {};
    }

    bool PropertyGridModel::isTargetInstance( const void* pInstance ) const
    {
        return pInstance != nullptr && pInstance == _pInstance && ( _pComponent != nullptr || _pObject != nullptr );
    }

    bool PropertyGridModel::isMixed( const PropertyInfo& prop )
    {
        return _isMixed.isBound() && _isMixed( prop );
    }

    const GameObjectManager* PropertyGridModel::findEditOwnerManager() const
    {
        return _pObject != nullptr ? _pObject->getManager() : nullptr;
    }

    ObjectAddress PropertyGridModel::findDefaultInstance( const void* pInstance, const TypeInfo& type ) const
    {
        // 컴포넌트이면 그 타입의 기본 인스턴스(CDO)와 비교한다. 중첩 구조체와 씬 밖 객체는 메타의 기본값 텍스트만 있다.
        const bool bIsEditedComponent = _pComponent != nullptr && pInstance == static_cast<const void*>( _pComponent );
        if ( bIsEditedComponent == false )
            return ObjectAddress{ nullptr };
        return ObjectAddress{ editor::getSubsystem<EditorContext>().getSubsystem<EditorDefaultObjects>().findDefault( type ) };
    }

    const vector<InspectorPropertyGroup>& PropertyGridModel::getOrCreateGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                                                string_view filterText, bool bMayEvict )
    {
        return _layoutCache.getOrCreateGroups( type, listDrawnName, filter, filterText, TypeRegistry::getGeneration(), bMayEvict );
    }

    const InspectorDisplayUnit& PropertyGridModel::getDisplayUnit( const PropertyInfo& prop )
    {
        const InspectorPropertyDisplay* pDisplay = _layoutCache.findDisplay( prop );
        if ( pDisplay != nullptr )
            return pDisplay->_unit;
        // 묶음 밖 — 컨테이너 원소 · 맵 값은 `makeElementProperty` 가 값으로 만든 임시라 주소로 캐시할 수 없다.
        _scratchDisplay._unit = InspectorPropertyLayout::getDisplayUnit( prop );
        return _scratchDisplay._unit;
    }

    const utf8* PropertyGridModel::getUnitNumberFormat( const PropertyInfo& prop, const utf8* pNumberFormat )
    {
        InspectorPropertyDisplay* pDisplay = _layoutCache.findDisplay( prop );
        if ( pDisplay == nullptr )
        {
            // 묶음 밖 임시 프로퍼티 — `getDisplayUnit` 과 같은 까닭으로 부를 때마다 짓는다.
            _scratchDisplay._unitFormat = InspectorPropertyLayout::appendUnitSuffix( pNumberFormat, InspectorPropertyLayout::getDisplayUnit( prop )._suffix );
            return _scratchDisplay._unitFormat.c_str();
        }
        if ( pDisplay->_pUnitFormatBase != pNumberFormat )
        {
            pDisplay->_unitFormat      = InspectorPropertyLayout::appendUnitSuffix( pNumberFormat, pDisplay->_unit._suffix );
            pDisplay->_pUnitFormatBase = pNumberFormat;
        }
        return pDisplay->_unitFormat.c_str();
    }

    bool PropertyGridModel::hasAnyFavoriteFor( const TypeInfo& type ) const
    {
        return _pFavorites != nullptr && _pFavorites->hasAnyForTypeOrBase( type );
    }

    bool PropertyGridModel::isFavorite( string_view favoriteKey ) const
    {
        return _pFavorites != nullptr && _pFavorites->contains( favoriteKey );
    }

    void PropertyGridModel::keepFavoriteProperties( const TypeInfo& type, vector<InspectorPropertyGroup>& inoutListGroup ) const
    {
        InspectorPropertyGroup favoriteGroup;
        favoriteGroup._category = type.getDisplayName();
        for ( const InspectorPropertyGroup& group : inoutListGroup )
        {
            for ( const PropertyInfo* pProp : group._listProperty )
            {
                if ( isFavorite( InspectorFavorites::makePropertyKey( type, *pProp ) ) )
                    favoriteGroup._listProperty.push_back( pProp );
            }
        }
        inoutListGroup.clear();
        if ( favoriteGroup._listProperty.empty() == false )
            inoutListGroup.push_back( std::move( favoriteGroup ) );
    }

    void PropertyGridModel::toggleFavorite( string_view favoriteKey )
    {
        if ( _pFavorites == nullptr )
            return;
        (void)_pFavorites->toggle( favoriteKey );
        (void)_pFavorites->saveToFile( InspectorFavorites::getDefaultFilePath() ); // 실패는 saveToFile 이 경고한다
    }

    void PropertyGridModel::removeDefaultValued( const ObjectAddress instance, const ObjectAddress defaultInstance, vector<InspectorPropertyGroup>& inoutListGroup )
    {
        for ( InspectorPropertyGroup& group : inoutListGroup )
        {
            VectorUtil::removeIf( group._listProperty, [this, instance, defaultInstance]( const PropertyInfo* pProp )
            { return isMixed( *pProp ) == false && EditorDefaultObjects::isDefaultValue( *pProp, instance, defaultInstance ); } );
        }
        VectorUtil::removeIf( inoutListGroup, []( const InspectorPropertyGroup& group )
        { return group._listProperty.empty(); } );
    }

    void PropertyGridModel::setPropertyText( const MutableObjectAddress instance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel )
    {
        // 그 프로퍼티 하나만 쓰고(비트필드는 그 비트만) 알린 뒤 되돌리기에 남긴다. `{"이름":값}` 을 JSON 으로 읽히면 읽기에서 빠진
        // 프로퍼티마다 기본값을 채우므로 다른 프로퍼티까지 바뀐다. 그래서 텍스트 하나로 프로퍼티 하나만 쓴다.
        const string value{ text };
        auto         apply = [this, &prop, instance, &value]()
        {
            if ( SerializerUtil::applyPropertyText( prop, instance, value, SerializeContext::getDefault( SerializePurpose::Authoring ) ) )
                notifyPropertyEdited( prop );
            else
                SW_LOG_WARNING_ID( "EditorPropertyGrid.CouldNotApplied", "'%#' could not be applied to %#", value.c_str(), prop._name.c_str() );
        };
        // 다중 편집 대상(`_isMixed` 가 걸렸다)은 `_onEdited` 가 값을 나머지에도 입힌다. 주 선택만 찍으면 되돌리기가 나머지를 돌리지 못하므로 선택 전부를 찍는다.
        if ( _isMixed.isBound() )
        {
            InspectorEditRecorder::recordImmediateEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
            apply();
        }
        else
            PropertyGridModelInternal::applyEdit( _pObject, pUndoLabel, apply );
    }

    void PropertyGridModel::resetToDefault( const MutableObjectAddress instance, const PropertyInfo& prop, const ObjectAddress defaultInstance )
    {
        const string text = defaultInstance.get() != nullptr ? SerializerUtil::formatPropertyText( prop, defaultInstance, SerializeContext::getDefault( SerializePurpose::Authoring ) )
                                                             : prop._metadata->_defaultValue;
        setPropertyText( instance, prop, text, "Reset to Default" );
    }

    void PropertyGridModel::notifyPropertyEdited( const PropertyInfo& prop )
    {
        if ( _pComponent != nullptr )
            _pComponent->onPropertyChanged( prop._name );
        else if ( _pObject != nullptr )
            _pObject->onPropertyChanged( prop._name );

        // 씬 밖 객체(환경설정, 문서)는 대상이 지정한 곳으로 알린다.
        if ( _onEdited.isBound() )
            _onEdited( prop );

        // 고친 값을 검증해 맵 검사 결과를 바꾼다. 로그는 남기지 않는다. 끄는 동안 프레임마다 호출되기 때문이다.
        if ( _pObject != nullptr )
            (void)ObjectValidation::reportGameObject( *_pObject, false );
    }

    void PropertyGridModel::setEnumValue( const PropertyInfo& prop, const MutableValueAddress enumMemory, const EnumInfo& enumInfo, int64 value )
    {
        // 콤보에서 고른 값은 고르는 순간 바뀐다 — 콤보 단추의 활성화 · 해제로는 잡히지 않으므로 쓰기 직전에 편집 전을 찍어 남긴다.
        InspectorEditRecorder::recordImmediateEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
        enumInfo.writeValueToMemory( enumMemory.get(), value );
    }

    void PropertyGridModel::addElement( const PropertyInfo& prop, const MutableValueAddress container )
    {
        ISequenceContainerWrapper* pSeq       = prop._containerWrapper->asSequence();
        void* const                pContainer = container.get();
        PropertyGridModelInternal::applyEdit( _pObject, "Add Element", [pSeq, pContainer]()
        { pSeq->addElementDefault( pContainer ); } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::clearElements( const PropertyInfo& prop, const MutableValueAddress container )
    {
        ISequenceContainerWrapper* pSeq       = prop._containerWrapper->asSequence();
        void* const                pContainer = container.get();
        PropertyGridModelInternal::applyEdit( _pObject, "Clear Elements", [pSeq, pContainer]()
        { pSeq->clear( pContainer ); } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::removeMapEntry( const PropertyInfo& prop, const MutableValueAddress container, size_t ordinal )
    {
        IMapContainerWrapper* pMap       = prop._containerWrapper->asMap();
        void* const           pContainer = container.get();
        PropertyGridModelInternal::applyEdit( _pObject, "Remove Map Entry", [pMap, pContainer, ordinal]()
        { (void)pMap->eraseAt( pContainer, ordinal ); } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::addMapEntry( const PropertyInfo& prop, const MutableValueAddress container, string_view keyText )
    {
        IMapContainerWrapper&   mapWrapper = *prop._containerWrapper->asMap();
        void* const             pContainer = container.get();
        const SerializeContext& context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        // 키를 키 타입으로 읽어 본다. 같은 키(텍스트가 같다)가 이미 있으면 값을 덮어쓰지 않는다.
        const MemoryBlock keyBlock   = MemoryBlock::allocateAligned( mapWrapper.getKeySize(), alignof( std::max_align_t ) );
        const MemoryBlock valueBlock = MemoryBlock::allocateAligned( mapWrapper.getValueSize(), alignof( std::max_align_t ) );
        void* const       pKey       = keyBlock.get();
        void* const       pValue     = valueBlock.get();
        mapWrapper.defaultConstructKey( pKey );
        mapWrapper.defaultConstructValue( pValue );
        if ( SerializerUtil::parseTextValue( MutableValueAddress{ pKey }, prop._keyTypeName, keyText, context ) )
        {
            const string stagedText = SerializerUtil::formatValueText( ValueAddress{ pKey }, prop._keyTypeName, context );
            bool         bExists    = false;
            mapWrapper.forEach( pContainer, SW_DELEGATE_LAMBDA( MapForEachDelegate, [&]( const void* pExistingKey, const void* )
            {
                if ( SerializerUtil::formatValueText( ValueAddress{ pExistingKey }, prop._keyTypeName, context ) == stagedText )
                    bExists = true;
            } ) );
            if ( bExists == false )
            {
                PropertyGridModelInternal::applyEdit( _pObject, "Add Map Entry", [&mapWrapper, pContainer, pKey, pValue]()
                { mapWrapper.insertKeyValue( pContainer, pKey, pValue ); } );
                notifyPropertyEdited( prop );
            }
        }
        mapWrapper.destroyKey( pKey );
        mapWrapper.destroyValue( pValue );
    }

    void PropertyGridModel::replaceKeyedElement( const PropertyInfo& prop, const MutableValueAddress container, size_t index, string_view text )
    {
        ISequenceContainerWrapper& sequence   = *prop._containerWrapper->asSequence();
        void* const                pContainer = container.get();
        const SerializeContext&    context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        PropertyGridModelInternal::applyEdit( _pObject, "Edit Set Element", [&]()
        {
            (void)sequence.replaceElement( pContainer, index, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
            { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, text, context ); } ) );
        } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::removeKeyedElement( const PropertyInfo& prop, const MutableValueAddress container, size_t index )
    {
        ISequenceContainerWrapper* pSeq       = prop._containerWrapper->asSequence();
        void* const                pContainer = container.get();
        PropertyGridModelInternal::applyEdit( _pObject, "Remove Set Element", [pSeq, pContainer, index]()
        { (void)pSeq->eraseAt( pContainer, index ); } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::addKeyedElement( const PropertyInfo& prop, const MutableValueAddress container, string_view text )
    {
        ISequenceContainerWrapper& sequence   = *prop._containerWrapper->asSequence();
        void* const                pContainer = container.get();
        const SerializeContext&    context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        PropertyGridModelInternal::applyEdit( _pObject, "Add Set Element", [&]()
        {
            (void)sequence.appendElement( pContainer, sequence.getSize( pContainer ), SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
            { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, text, context ); } ) );
        } );
        notifyPropertyEdited( prop );
    }

    void PropertyGridModel::invokeMethod( void* pInstance, const TypeInfo& type, const FunctionInfo& method, const TaskArgs& args )
    {
        TaskValue  result{};
        const auto call = [&]()
        { result = getTypeRegistry().invokeMethod( pInstance, type._fullyQualifiedName, method._hashName, args ); };
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "Call %#", method._name.c_str() );
        PropertyGridModelInternal::applyEdit( _pObject, textBuilder.view(), call ); // 오브젝트 밖 대상(환경설정 구조체 등)은 되돌리기 대상이 없다
        InspectorBuiltinValue::formatMethodResult( result, method._returnTypeName, _lastInvokeResult.data(), _lastInvokeResult.capacity() );
    }

    void PropertyGridModel::broadcastEvent( const EventInfo& event, void* pInstance )
    {
        (void)ReflectionInvoke::broadcastEvent( event, pInstance, {} );
    }

    InspectorPropertyManager& PropertyGridModel::getPropertyManager()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<InspectorPropertyManager>();
    }

    const TypeRegistry& PropertyGridModel::getTypeRegistry()
    {
        return editor::getSubsystem<TypeRegistry>();
    }
} // namespace sw::editor
