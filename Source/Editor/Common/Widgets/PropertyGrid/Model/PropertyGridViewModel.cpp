#include "pch.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Commands/Asset/EditorAssetCatalog.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorBuiltinValue.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorEditRecorder.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorFavorites.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorObjectEdit.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditConditionExpr.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditStep.h"
#include "Editor/Common/Widgets/PropertyGrid/PropertyDrawerRegistry.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Inherit/DataCatalogReferences.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"
#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct PropertyGridViewModelInternal
        {
            /** @brief 화면 밖 판정에 더하는 여유(보이는 높이에 대한 비) — 스크롤이 한 프레임에 움직이는 만큼은 미리 채워 빈 줄이 비치지 않게 한다. */
            static constexpr float32 kOffscreenMarginRatio = 0.5f;

            /** @brief 대상 범위의 씬입니다. 미리보기 범위는 지금 겨눈 미리보기 선택의 씬이고, 겨눈 창이 미리보기가 아니면 없습니다. */
            static Scene* findScene( EditorPropertyGridScope scope )
            {
                if ( scope == EditorPropertyGridScope::None )
                    return nullptr;
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

            /** @brief 게임 단계에서 다시 찾은 씬 대상입니다 — 주인 오브젝트, 컴포넌트(오브젝트 자신이면 nullptr), 인스턴스와 타입. */
            struct SceneTarget
            {
                GameObject*     _pObject{ nullptr };
                Component*      _pComponent{ nullptr };
                void*           _pInstance{ nullptr };
                const TypeInfo* _pType{ nullptr };
            };

            /** @brief @p scope 씬의 오브젝트 @p objectID 의 컴포넌트 @p componentID(0 이면 오브젝트 자신)를 찾습니다. 그 사이 사라졌거나 지우는 중이면 false 입니다. */
            static bool resolveSceneTarget( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, SceneTarget& outTarget )
            {
                GameObject* pObject = findObject( scope, objectID );
                if ( pObject == nullptr || pObject->isPendingDestroy() )
                    return false;
                Component* pComponent = componentID != 0 ? pObject->findComponentByID( componentID ) : nullptr;
                if ( componentID != 0 && pComponent == nullptr )
                    return false;
                outTarget._pObject    = pObject;
                outTarget._pComponent = pComponent;
                outTarget._pInstance  = pComponent != nullptr ? static_cast<void*>( pComponent ) : static_cast<void*>( pObject );
                outTarget._pType      = pComponent != nullptr ? pComponent->getTypeInfo() : pObject->getTypeInfo();
                return outTarget._pType != nullptr;
            }

            /**
             * @brief 씬 대상 편집 하나를 적용합니다(게임 단계) — 대상을 id 로 다시 찾고, 경로를 풀어 쓰고, 모델의 쓰기 길대로 알린다(깊이 0 프로퍼티로).
             * @details 다중 선택은 깊이 0 프로퍼티를 통째로 나머지에 입힌다(예전 `copyPropertyToOthers` 와 같은 단위 — 되돌리기 구간도 그 단위다). 그 사이 사라진 대상은 건너뛴다.
             */
            static void applySceneEdit( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, uint32 sceneKey, const vector<PropertyEditTarget>& listOther,
                                        const PropertyEditRequest& request )
            {
                SceneTarget target;
                if ( resolveSceneTarget( scope, objectID, componentID, target ) == false )
                    return;
                GameObject* const     pObject    = target._pObject;
                Component* const      pComponent = target._pComponent;
                const TypeInfo* const pType      = target._pType;
                void* const           pInstance  = target._pInstance;
                if ( request.applyTo( MutableObjectAddress{ pInstance }, *pType, PropertyGridViewModel::getTypeRegistry() ) == false )
                    return;
                // 이 알림을 빠뜨리면 값에 반응하는 상태(트랜스폼 캐시 · 렌더 상태)가 어긋난다. 검증은 로그 없이 맵 검사 결과만 바꾼다(끄는 동안 프레임마다 온다).
                if ( pComponent != nullptr )
                    pComponent->onPropertyChanged( request._path._root );
                else
                    pObject->onPropertyChanged( request._path._root );
                (void)ObjectValidation::reportGameObject( *pObject, false );
                if ( listOther.empty() )
                    return;
                const PropertyInfo* pRootProp = pType->findPropertyInHierarchy( request._path._root );
                if ( pRootProp == nullptr )
                    return;
                const string text = SerializerUtil::formatPropertyText( *pRootProp, ObjectAddress{ pInstance }, SerializeContext::getDefault( SerializePurpose::Authoring ) );
                const string path{ request._path._root.c_str() };
                for ( const PropertyEditTarget& other : listOther )
                {
                    GameObject* pOther = PropertyEditTarget::findObject( sceneKey, other._objectID );
                    if ( pOther != nullptr )
                        (void)other.applyTo( *pOther, path, text ); // 알림 · 검증은 `applyTo` 가 한다
                }
            }

            /** @brief 씬 대상 편집 하나를 게임 단계에 넘깁니다(`EditorCommandRegistry::requestSceneEdit`) — 람다는 범위 · id · 요청 값만 붙잡는다. */
            static void requestSceneEdit( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, uint32 sceneKey, vector<PropertyEditTarget>&& listOther,
                                          PropertyEditRequest&& request )
            {
                EditorCommandRegistry::getInEditorContext().requestSceneEdit( SW_DELEGATE_LAMBDA( Delegate<void( EditorTransaction& )>, [scope, objectID, componentID, sceneKey, listOther = std::move( listOther ), request = std::move( request )]( EditorTransaction& /*transaction*/ )
                { applySceneEdit( scope, objectID, componentID, sceneKey, listOther, request ); } ) );
            }

            /**
             * @brief 씬 대상의 FUNCTION 호출 하나를 게임 단계에 넘깁니다. 호출은 오브젝트 편집 하나로 감싼다(바뀐 것이 있으면 되돌리기 한 단계 — 플레이 중에는 기록 없이,
             *        `InspectorObjectEdit::applyObjectEdit`). 선언으로 고르지 않는다 — 모든 호출이 기록 대상이라 "선언을 빠뜨린 변경 호출" 이 없다(언리얼 CallInEditor 의
             *        `FScopedTransaction` 과 같다). 결과는 @p pResult 칸(그리드가 가진 결과 칸 — 다음 UI 프레임이 읽는다)에 둔다. 람다는 id · 이름 · 인자 값만 붙잡는다.
             */
            static void requestInvoke( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, uint32 sceneKey, vector<PropertyEditTarget>&& listOther,
                                       const hashed_string& methodName, const string& label, const vector<ReflectedValue>& listArg,
                                       const shared_ptr<InspectorInvokeResult>& pResult )
            {
                EditorCommandRegistry::getInEditorContext().requestSceneEdit( SW_DELEGATE_LAMBDA( Delegate<void( EditorTransaction& )>, [scope, objectID, componentID, sceneKey, listOther = std::move( listOther ), methodName, label, listArg, pResult]( EditorTransaction& transaction )
                {
                    pResult->begin( methodName.c_str() );
                    SceneTarget target;
                    if ( resolveSceneTarget( scope, objectID, componentID, target ) == false )
                        return;
                    invokeOn( transaction, target._pObject, *target._pType, target._pInstance, methodName, label, listArg, *pResult );
                    // 다중 선택의 나머지 — 한 거래라 되돌리기는 한 단계다. 그 사이 사라진 대상은 건너뛴다(세지 않는다).
                    for ( const PropertyEditTarget& other : listOther )
                    {
                        GameObject* pOther = PropertyEditTarget::findObject( sceneKey, other._objectID );
                        if ( pOther == nullptr )
                            continue;
                        Component*      pComponent = nullptr;
                        const TypeInfo* pType      = nullptr;
                        void*           pInstance  = other.findInstance( *pOther, pComponent, pType );
                        if ( pInstance != nullptr && pType != nullptr )
                            invokeOn( transaction, pOther, *pType, pInstance, methodName, label, listArg, *pResult );
                    }
                } ) );
            }

            /** @brief 대상 하나에 이름으로 부르고(`ReflectionInvoke::callByName` — 없는 함수 · 틀린 인자는 결과 갈래로 온다) 결과를 더합니다. */
            static void invokeOn( EditorTransaction& transaction, GameObject* pObject, const TypeInfo& type, void* pInstance, const hashed_string& methodName,
                                  const string& label, const vector<ReflectedValue>& listArg, InspectorInvokeResult& outResult )
            {
                ReflectCallResult result = ReflectCallResult::NotBound;
                ReflectedValue    returnValue;
                InspectorObjectEdit::applyObjectEdit( transaction, pObject, label, [&]()
                { result = ReflectionInvoke::callByName( type, pInstance, methodName, listArg, &returnValue ); } );
                outResult.record( result, returnValue );
            }

            /** @brief 씬 대상의 이벤트 방송 하나를 게임 단계에 넘깁니다(기록하지 않는다 — 받는 쪽이 할 일이다). 이벤트는 이름으로 그때 다시 찾는다. */
            static void requestBroadcast( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, const hashed_string& eventName )
            {
                EditorCommandRegistry::getInEditorContext().requestSceneEdit( SW_DELEGATE_LAMBDA( Delegate<void( EditorTransaction& )>, [scope, objectID, componentID, eventName]( EditorTransaction& /*transaction*/ )
                {
                    SceneTarget target;
                    if ( resolveSceneTarget( scope, objectID, componentID, target ) == false )
                        return;
                    const EventInfo* pEvent = target._pType->findEventInHierarchy( eventName );
                    if ( pEvent != nullptr )
                        (void)ReflectionInvoke::broadcastEvent( *pEvent, target._pInstance, {} );
                } ) );
            }

            /** @brief 열거형 값의 미리보기 글입니다. 비트 플래그는 켜진 이름을 ` | ` 로 잇고, 하나도 없으면 `None`(0) · `<Unknown>` 입니다. */
            static void formatEnumPreview( const TypeRegistry& registry, const EnumInfo& enumInfo, const PropertyInfo& prop, int64 value, string& outText )
            {
                outText.clear();
                if ( enumInfo._bIsBitFlag == false )
                {
                    const utf8* pName = registry.enumToString( prop._typeName, value );
                    outText           = pName != nullptr ? pName : "<Unknown>";
                    return;
                }
                // 저장 글과 같은 규칙(단일 비트 이름만, `EnumInfo::collectFlagNames`)으로 잇는다 — 조합 열거자를 겹쳐 보이지 않는다.
                const string_view flagsText = enumInfo.toStringFlags( value );
                outText                     = flagsText.empty() ? string_view( "<Unknown>" ) : flagsText;
            }

            /** @brief 그릴 위젯이 없는 타입의 글입니다. */
            static string makeNoInspectorText( const PropertyInfo& prop )
            {
                return SW_LOCKEYFORMAT( "EdFormat", "NoInspectorFor", TextArgumentList().addText( "type", prop._typeName.c_str() ) );
            }

            /** @brief 내장 값의 읽기 전용 글입니다. 숫자 · 벡터는 표시 단위(@p scale)로 보인다. */
            static void formatReadOnlyBuiltin( const InspectorBuiltinValue& builtin, const void* pValue, float32 scale, string& outText )
            {
                fixed_string<constant::buffer::kMax512> buf;
                switch ( builtin._widget )
                {
                    case InspectorValueWidget::Number:
                    {
                        float64                   number = 0.0;
                        int64                     integer{ 0 };
                        const ReflectBuiltinIndex index = static_cast<ReflectBuiltinIndex>( builtin._index );
                        if ( ReflectedValueUtil::readInteger( index, pValue, integer ) )
                            formatstring( buf.data(), buf.capacity(), "%#", integer );
                        else if ( ReflectedValueUtil::readNumber( index, pValue, number ) )
                            formatstring( buf.data(), buf.capacity(), "%#", number * static_cast<float64>( scale ) );
                        break;
                    }
                    case InspectorValueWidget::Vector2:
                    {
                        const float2& value = *static_cast<const float2*>( pValue );
                        formatstring( buf.data(), buf.capacity(), "(%#, %#)", Fmt( value._x, Format( 2 ) ), Fmt( value._y, Format( 2 ) ) );
                        break;
                    }
                    case InspectorValueWidget::Vector3:
                    {
                        const float3 shown = *static_cast<const float3*>( pValue ) * scale;
                        formatstring( buf.data(), buf.capacity(), "(%#, %#, %#)", Fmt( shown._x, Format( 2 ) ), Fmt( shown._y, Format( 2 ) ), Fmt( shown._z, Format( 2 ) ) );
                        break;
                    }
                    case InspectorValueWidget::Vector4:
                    {
                        const float4& value = *static_cast<const float4*>( pValue );
                        formatstring( buf.data(), buf.capacity(), "(%#, %#, %#, %#)", Fmt( value._x, Format( 2 ) ), Fmt( value._y, Format( 2 ) ), Fmt( value._z, Format( 2 ) ),
                                      Fmt( value._w, Format( 2 ) ) );
                        break;
                    }
                    default:
                        builtin._pFormatValue( pValue, buf.data(), buf.capacity() );
                        break;
                }
                outText = buf.c_str();
            }

            /** @brief 저장 꼴이 GUID 참조인 칸 형식(`AssetRef<T>` · `DataPath`)이면 true 입니다. */
            static bool isAssetReference( const hashed_string& typeName ) { return typeName == hashed_string( AssetRefBase::kTypeName ) || typeName == hashed_string( "DataPath" ); }

            /** @brief 칸 값(맵 키 · 시퀀스 원소도)의 표시 글입니다. 에셋 참조는 경로를 보인다 — 저장 꼴(`guid:<32자>`)은 사람이 읽지 못하고, 고친 글은 경로로 다시 넣는다(`AssetRegistry::readReferenceText`). */
            static string formatValueText( const void* pValue, const hashed_string& typeName )
            {
                if ( typeName == hashed_string( AssetRefBase::kTypeName ) )
                    return static_cast<const AssetRefBase*>( pValue )->path();
                if ( isAssetReference( typeName ) )
                {
                    const DataPath& path = *static_cast<const DataPath*>( pValue );
                    return path.isSet() ? path.get() : string{};
                }
                return SerializerUtil::formatValueText( ValueAddress{ pValue }, typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) );
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

    PropertyGridViewModel::PropertyGridViewModel()
        : _pComponent{ nullptr }
        , _pObject{ nullptr }
        , _pInstance{ nullptr }
        , _pType{ nullptr }
        , _onEdited{}
        , _isMixed{}
        , _collectEditTargets{}
        , _editSpans{}
        , _pFavorites{ nullptr }
        , _pActionFavorites{ nullptr }
        , _layoutCache{}
        , _scratchUnitFormat{}
        , _listFilteredGroup{}
        , _listBinding{}
        , _listElementProp{}
        , _mapRowHeight{}
        , _setFlippedKey{}
        , _setReportedHandle{}
        , _targetSalt{ 0 }
        , _targetObjectID{ 0 }
        , _targetComponentID{ 0 }
        , _targetScope{ EditorPropertyGridScope::None }
        , _pLastInvokeResult{ make_shared<InspectorInvokeResult>() }
    {
    }

    PropertyGridViewModel::~PropertyGridViewModel() = default;

    bool PropertyGridViewModel::beginTarget( const EditorPropertyGridTarget& target )
    {
        endTarget();
        if ( target._scope == EditorPropertyGridScope::None )
        {
            if ( target._pInstance == nullptr || target._pType == nullptr )
                return false;
            _pInstance = target._pInstance;
            _pType     = target._pType;
            // 씬 밖 대상은 타입으로 가른다 — 주인 패널이 인스턴스를 다시 만들어도(문서 다시 읽기) 펼침 · 높이가 남는다(예전 ImGui id 도 주소와 무관했다).
            _targetSalt = HashUtil::combine( 0, static_cast<uint64>( target._pType->_fullyQualifiedName.getHash() ) );
        }
        else
        {
            GameObject* pObject = findSceneObject( target._scope, target._objectID );
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
            _targetSalt = HashUtil::combine( HashUtil::combine( static_cast<uint64>( target._scope ), target._objectID ), target._componentID );
            // 쓰기 요청은 포인터가 아니라 이 값으로 대상을 다시 찾는다(`submitEdit`).
            _targetScope       = target._scope;
            _targetObjectID    = target._objectID;
            _targetComponentID = target._componentID;
        }
        _onEdited           = target._onEdited;
        _isMixed            = target._isMixed;
        _collectEditTargets = target._collectEditTargets;
        return true;
    }

    void PropertyGridViewModel::endTarget()
    {
        _pComponent         = nullptr;
        _pObject            = nullptr;
        _pInstance          = nullptr;
        _pType              = nullptr;
        _onEdited           = {};
        _isMixed            = {};
        _collectEditTargets = {};
        _targetScope        = EditorPropertyGridScope::None;
        _targetObjectID     = 0;
        _targetComponentID  = 0;
        _listBinding.clear();
        _listElementProp.clear();
    }

    bool PropertyGridViewModel::isTargetInstance( const void* pInstance ) const
    {
        return pInstance != nullptr && pInstance == _pInstance && ( _pComponent != nullptr || _pObject != nullptr );
    }

    bool PropertyGridViewModel::isMixed( const PropertyInfo& prop )
    {
        return _isMixed.isBound() && _isMixed( prop );
    }

    const GameObjectManager* PropertyGridViewModel::findEditOwnerManager() const
    {
        return _pObject != nullptr ? _pObject->getMutableManager() : nullptr;
    }

    ObjectAddress PropertyGridViewModel::findDefaultInstance( const void* pInstance, const TypeInfo& type ) const
    {
        // 컴포넌트이면 그 타입의 기본 인스턴스(CDO)와 비교한다. 중첩 구조체와 씬 밖 객체는 메타의 기본값 텍스트만 있다.
        const bool bIsEditedComponent = _pComponent != nullptr && pInstance == static_cast<const void*>( _pComponent );
        if ( bIsEditedComponent == false )
            return ObjectAddress{ nullptr };
        return ObjectAddress{ editor::getSubsystem<EditorContext>().getSubsystem<EditorDefaultObjects>().findDefault( type ) };
    }

    // ------------------------------------------------------------------------------
    // 값 행
    // ------------------------------------------------------------------------------
    void PropertyGridViewModel::gather( const PropertyGridGatherParams& params, PropertyGridViewState& outState )
    {
        outState._rowCount    = 0;
        outState._filledCount = 0;
        outState._bNoMatch    = SW_FALSE;
        outState._bHasRows    = SW_FALSE;
        _listBinding.clear();
        _listElementProp.clear();
        if ( _pInstance == nullptr || _pType == nullptr )
            return;
        const TypeInfo& type = *_pType;

        // 무엇을 어떤 순서로 그릴지는 `InspectorPropertyLayout` 이 정한다. 상속한 프로퍼티까지 포함하고, 카테고리는 기반 클래스부터 처음 나온 순서다.
        // 묶음은 (타입, 그린 이름, 검색 글, 레지스트리 세대)가 같으면 캐시한 것을 쓴다. 즐겨찾기 · 바뀐 것만 보기는 그 사본을 거른다.
        static const vector<hashed_string>    s_listNoDrawnName{};
        const EditorListFilter                filter{ params._filterText };
        const vector<InspectorPropertyGroup>& listCachedGroup =
            _layoutCache.getOrCreateGroups( type, params._pListDrawnName != nullptr ? *params._pListDrawnName : s_listNoDrawnName, filter, params._filterText,
                                            TypeRegistry::getGeneration(), true );
        const vector<InspectorPropertyGroup>* pListGroup     = &listCachedGroup;
        const bool                            bFavoritesOnly = params._bFavoritesOnly && hasFavorites();
        if ( bFavoritesOnly )
        {
            _listFilteredGroup = listCachedGroup;
            pListGroup         = &_listFilteredGroup;
            keepFavoriteProperties( type, _listFilteredGroup );
            if ( _listFilteredGroup.empty() )
                return;
        }
        if ( pListGroup->empty() )
        {
            outState._bNoMatch = filter.isActive() ? SW_TRUE : SW_FALSE;
            return;
        }
        // 컴포넌트이면 그 타입의 기본 인스턴스(CDO)와 비교한다. 중첩 구조체와 씬 밖 객체는 메타의 기본값 텍스트만 있다.
        const MutableObjectAddress instance{ _pInstance };
        const ObjectAddress        defaultInstance = findDefaultInstance( _pInstance, type );
        if ( params._bModifiedOnly )
        {
            if ( pListGroup != &_listFilteredGroup )
            {
                _listFilteredGroup = listCachedGroup;
                pListGroup         = &_listFilteredGroup;
            }
            removeDefaultValued( instance, defaultInstance, _listFilteredGroup );
            if ( _listFilteredGroup.empty() )
                return;
        }
        outState._bHasRows = SW_TRUE;

        // 행 메뉴의 즐겨찾기 토글은 대상 자신(컴포넌트나 오브젝트)의 프로퍼티에만 둔다. 중첩 구조체의 필드는 키가 대상 타입이 아니다.
        const bool    bFavoriteTarget = hasFavorites() && isTargetInstance( _pInstance );
        const uint64  salt            = HashUtil::combine( _targetSalt, bFavoritesOnly ? 1u : 0u ); // 즐겨찾기 구역은 본문과 펼침을 나누지 않는다(ImGui id 도 다르다)
        const auto&   window          = params._window;
        const float32 margin          = ( window._clipMaxY - window._clipMinY ) * PropertyGridViewModelInternal::kOffscreenMarginRatio;
        float32       rowY            = window._originY;
        const auto    isOnScreen      = [&window, margin]( float32 top, float32 height )
        { return window._bSkipOffscreen == SW_FALSE || ( top + height >= window._clipMinY - margin && top <= window._clipMaxY + margin ); };

        for ( const InspectorPropertyGroup& group : *pListGroup )
        {
            const uint64      categoryKey     = HashUtil::combine( salt, HashUtil::computeStringHash64( group._category, false ) );
            PropertyValueRow& category        = appendRow( outState, PropertyRowKind::Category, 0, categoryKey );
            _listBinding.back()._bDefaultOpen = 1; // 카테고리는 처음에 펼쳐져 있다(언리얼 Details 와 같다)
            if ( params._openAllRequest != OpenAllRequest::None )
                storeExpanded( categoryKey, true, params._openAllRequest == OpenAllRequest::ExpandAll );
            const bool bCategoryOpen = isExpanded( categoryKey, true );
            category._label          = group._category;
            category._bExpanded      = bCategoryOpen ? SW_TRUE : SW_FALSE;
            rowY += findRowHeight( categoryKey, window._headerHeight );
            if ( bCategoryOpen == false )
                continue;

            for ( const InspectorPropertyItem& item : group._listItem )
            {
                const PropertyInfo& prop = *item._pProperty;
                // `EditCondition` 이 거짓이면 숨기거나(EditConditionHides) 막는다. 판정은 ImGui 에 의존하지 않는 `PropertyEditConditionExpr` 이 한다.
                const PropertyEditState editState = PropertyEditConditionExpr::getEditState( prop, instance );
                if ( editState == PropertyEditState::Hidden )
                    continue;
                const uint64  rowKey    = HashUtil::combine( salt, static_cast<uint64>( prop._name.getHash() ) );
                const float32 rowHeight = findRowHeight( rowKey, window._rowHeight );
                const bool    bOnScreen = isOnScreen( rowY, rowHeight );
                rowY += rowHeight;
                if ( bOnScreen == false )
                {
                    PropertyValueRow& skipped = appendRow( outState, PropertyRowKind::Skipped, 0, rowKey );
                    skipped._height           = rowHeight;
                    skipped._propName         = prop._name;
                    if ( window._bNameSkippedRows )
                    {
                        skipped._markName = type._name.c_str();
                        skipped._markName += ".";
                        skipped._markName += prop._name.c_str();
                    }
                    continue;
                }

                const uint32 rowID = outState._rowCount;
                appendValueRow( outState, instance, prop, 0, constant::invalid_index::kUint32, rowKey, PropertyRowChild::None, editState == PropertyEditState::Disabled, item._pDisplay );
                // 깊이 0 행만의 값: 이름 칸(혼합 · 즐겨찾기 · 되돌리기 단추 · 표식). 자식이 행을 더했어도 번호로 다시 찾는다.
                PropertyValueRow& row = outState._listRow[rowID];
                row._markName         = type._name.c_str();
                row._markName += ".";
                row._markName += prop._name.c_str();
                row._bMixed            = isMixed( prop ) ? SW_TRUE : SW_FALSE;
                row._favoriteKey       = item._pDisplay->_favoriteKey; // 묶음을 지을 때 만든 키
                row._bFavoriteTarget   = bFavoriteTarget ? SW_TRUE : SW_FALSE;
                row._bFavorite         = ( bFavoriteTarget && isFavorite( row._favoriteKey ) ) ? SW_TRUE : SW_FALSE;
                row._bModified         = EditorDefaultObjects::isDefaultValue( prop, instance, defaultInstance ) ? SW_FALSE : SW_TRUE;
                const bool bHasDefault = defaultInstance.get() != nullptr || prop._metadata->_defaultValue.empty() == false;
                row._bCanReset         = ( bHasDefault && prop._bIsContainer == SW_FALSE ) ? SW_TRUE : SW_FALSE; // 되돌리기는 값 글을 넣는 편집이라 컨테이너에는 길이 없다(`PropertyEditKind::SetText`)
                row._bTransient        = prop._metadata->_bTransient == SW_TRUE ? SW_TRUE : SW_FALSE;
                // 타입 핸들은 선택기만이 입력 길이다. 글로 붙이면 대상 타입을 확인하지 않은 값이 들어간다.
                row._bPasteAllowed = prop._handleTargetTypeName.empty() ? SW_TRUE : SW_FALSE;
            }
        }
    }

    PropertyValueRow& PropertyGridViewModel::appendRow( PropertyGridViewState& outState, PropertyRowKind kind, uint16 depth, uint64 key )
    {
        if ( outState._rowCount == outState._listRow.size() )
            outState._listRow.emplace_back();
        const uint32      rowID = outState._rowCount++;
        PropertyValueRow& row   = outState._listRow[rowID];
        row.clear();
        row._kind        = kind;
        row._depth       = depth;
        row._key         = key;
        row._rowID       = rowID;
        row._parentRowID = rowID;
        RowBinding binding{};
        binding._key         = key;
        binding._rootRowID   = rowID;
        binding._parentRowID = rowID; // 자식 행은 부모가 고쳐 쓴다(`appendChildRows`)
        _listBinding.push_back( binding );
        if ( kind != PropertyRowKind::Category && kind != PropertyRowKind::Skipped )
            ++outState._filledCount;
        return row;
    }

    void PropertyGridViewModel::appendValueRow( PropertyGridViewState& outState, const MutableObjectAddress instance, const PropertyInfo& prop, uint16 depth, uint32 rootRowID, uint64 key,
                                                PropertyRowChild child, bool bDisabled, InspectorPropertyDisplay* pDisplay )
    {
        using Internal = PropertyGridViewModelInternal;

        PropertyValueRow& row   = appendRow( outState, PropertyRowKind::Unreadable, depth, key );
        const uint32      rowID = row._rowID;
        RowBinding&       bind  = _listBinding.back();
        bind._instance          = instance;
        bind._pProp             = &prop;
        bind._pDisplay          = pDisplay;
        bind._rootRowID         = rootRowID == constant::invalid_index::kUint32 ? rowID : rootRowID;
        bind._child             = child;

        const bool bReadOnly = prop._metadata->_bReadOnly != SW_FALSE;
        row._child           = child;
        row._label           = InspectorPropertyLayout::getPropertyLabel( prop );
        row._tooltip         = prop._metadata->getEditor()._tooltip.c_str();
        row._typeName        = prop._typeName;
        row._propName        = prop._name;
        row._bReadOnly       = bReadOnly ? SW_TRUE : SW_FALSE;
        row._bDisabled       = bDisabled ? SW_TRUE : SW_FALSE;

        // 위젯 종류는 그리기 순서와 같다: 내장 값 → 그리기 확장 → 열거형 → 컨테이너 → 구조체 → 없음(`EditorPropertyGrid` 가 확장을 먼저 부른다).
        const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() );
        if ( pBuiltin != nullptr )
        {
            fillBuiltinRow( row, instance, prop, *pBuiltin, pDisplay );
            return;
        }
        if ( getPropertyManager().find( prop._typeName.c_str() ) != nullptr )
        {
            // 확장(`IInspectorProperty`)이 맡는 타입 — 값은 글로 넘긴다(확장이 자기 형식으로 읽는다).
            row._kind             = PropertyRowKind::Text;
            const void* pAssetRef = PropertyGridViewModelInternal::isAssetReference( prop._typeName ) ? prop.getValuePtr<void>( instance.get() ) : nullptr;
            row._valueText        = pAssetRef != nullptr ? PropertyGridViewModelInternal::formatValueText( pAssetRef, prop._typeName )
                                                         : SerializerUtil::formatPropertyText( prop, instance, SerializeContext::getDefault( SerializePurpose::Authoring ) );
            row._bAssetPath       = ( prop._metadata->_bAssetPath != SW_FALSE || prop._metadata->_assetType.empty() == false ) ? SW_TRUE : SW_FALSE;
            return;
        }

        const TypeRegistry& registry  = getTypeRegistry();
        const EnumInfo*     pEnumInfo = registry.findEnum( prop._typeName );
        if ( pEnumInfo != nullptr )
        {
            // enum 의 실제 크기(`EnumInfo::_size` 와 부호)로 읽는다. 항상 int32 로 읽으면 이웃 바이트가 섞여 멀쩡한 값이 "<Unknown>" 으로 보인다.
            const void* pEnumMemory = prop.getValuePtr<void>( instance.get() );
            if ( pEnumMemory == nullptr )
                return;
            row._kind      = pEnumInfo->_bIsBitFlag ? PropertyRowKind::BitFlagEnum : PropertyRowKind::Enum;
            row._enumValue = pEnumInfo->readValueFromMemory( pEnumMemory );
            Internal::formatEnumPreview( registry, *pEnumInfo, prop, row._enumValue, row._valueText );
            // 선언 순서로 보이고 `Count` 표식은 선택지가 아니다(`EnumInfo::_listEnumerator`). `ENUMERATOR( Hidden )` 은 숨기고 표시 이름을 쓴다.
            for ( const EnumeratorInfo& enumerator : pEnumInfo->_listEnumerator )
            {
                const EnumeratorEditorMetadata& display = pEnumInfo->findEnumeratorEditorMetadata( enumerator._name );
                if ( display._bHidden != SW_FALSE )
                    continue;
                const string label = display._displayName.empty() ? string( enumerator._name.toString() ) : display._displayName;
                row._listChoice.push_back( PropertyEnumChoice{ enumerator._name, label, display._tooltip, enumerator._value } );
            }
            return;
        }

        if ( prop._bIsContainer && prop._containerWrapper != nullptr )
        {
            // 모든 분기가 무언가를 보인다. 아무것도 보이지 않으면 값이 비었는지, 그리지 못하는 것인지 화면만 보고는 구분할 수 없다.
            void* pContainer = prop.getRawPtr( instance.get() );
            if ( pContainer == nullptr )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "컨테이너를 읽을 수 없습니다 (%#)", prop._typeName.c_str() );
                row._valueText = text.c_str();
                return;
            }
            fixed_string<constant::buffer::kMax128> header;
            const ISequenceContainerWrapper*        pSeq = prop._containerWrapper->asSequence();
            if ( pSeq == nullptr )
            {
                const IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
                if ( pMap == nullptr )
                {
                    row._valueText = Internal::makeNoInspectorText( prop );
                    return;
                }
                row._kind         = PropertyRowKind::Map;
                row._elementCount = static_cast<uint32>( pMap->getSize( pContainer ) );
                formatstring( header.data(), header.capacity(), "[%# -> %#] (%# entries)", prop._keyTypeName.c_str(), prop._elementTypeName.c_str(), row._elementCount );
                // 열거자마다 한 칸인 맵(`EnumArray`)은 항목을 더하거나 지울 수 없다 — 값만 고친다.
                row._bCanAddRemove = ( bReadOnly == false && pMap->isFixedSize() == false ) ? SW_TRUE : SW_FALSE;
            }
            else
            {
                // 원소가 곧 정렬 키이거나 해시 키인 컨테이너(`set`)는 제자리에서 고칠 수 없으므로 지우고 다시 넣는다.
                const bool bKeyed = pSeq->allowsInPlaceElementWrite() == false;
                row._kind         = bKeyed ? PropertyRowKind::KeyedSequence : PropertyRowKind::Sequence;
                row._elementCount = static_cast<uint32>( pSeq->getSize( pContainer ) );
                // 고정 배열은 원소 수가 정해져 있다. 추가와 비우기는 아무 일도 하지 않으므로 두지 않는다.
                row._bCanAddRemove = ( bReadOnly == false && ( bKeyed || pSeq->isFixedSize() == false ) ) ? SW_TRUE : SW_FALSE;
                formatstring( header.data(), header.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), row._elementCount );
            }
            row._valueText = header.c_str();
        }
        else
        {
            const TypeInfo* pFieldType = registry.findType( prop._typeName );
            if ( pFieldType == nullptr )
            {
                row._valueText = Internal::makeNoInspectorText( prop );
                return;
            }
            if ( prop.getRawPtr( instance.get() ) == nullptr )
                return;
            row._kind = PropertyRowKind::Struct;
            fixed_string<constant::buffer::kMax128> header;
            formatstring( header.data(), header.capacity(), "[%#]", prop._typeName.c_str() );
            row._valueText = header.c_str();
        }

        // 노드(구조체 · 컨테이너): 펼쳐 있으면 자식 행을 바로 뒤에 낸다. 여기부터 `row` 는 자식이 행을 더하면 옮겨질 수 있다.
        const bool bExpanded = isExpanded( key, false );
        row._bExpanded       = bExpanded ? SW_TRUE : SW_FALSE;
        if ( bExpanded )
            appendChildRows( outState, rowID, instance, prop );
    }

    void PropertyGridViewModel::appendChildRows( PropertyGridViewState& outState, uint32 rowID, const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        using Internal = PropertyGridViewModelInternal;

        const uint32 rootRowID  = _listBinding[rowID]._rootRowID;
        const uint64 parentKey  = _listBinding[rowID]._key;
        const uint16 childDepth = static_cast<uint16>( outState._listRow[rowID]._depth + 1 );
        const bool   bDisabled  = outState._listRow[rowID]._bDisabled != SW_FALSE;
        const bool   bReadOnly  = prop._metadata->_bReadOnly != SW_FALSE;
        const auto   setParent  = [&outState, rowID]( uint32 childRowID )
        { outState._listRow[childRowID]._parentRowID = rowID; };

        if ( prop._bIsContainer == false || prop._containerWrapper == nullptr )
        {
            const TypeInfo* pFieldType = getTypeRegistry().findType( prop._typeName );
            void*           pNestedPtr = prop.getRawPtr( instance.get() );
            if ( pFieldType == nullptr || pNestedPtr == nullptr )
                return;
            pFieldType->forEachProperty( [&]( const PropertyInfo& nestedProp )
            {
                // 구조체 필드의 값 주소는 그 구조체 타입의 객체 주소다.
                const PropertyEditState editState = PropertyEditConditionExpr::getEditState( nestedProp, ObjectAddress{ pNestedPtr } );
                if ( editState == PropertyEditState::Hidden )
                    return;
                const uint32 childRowID = outState._rowCount;
                appendValueRow( outState, MutableObjectAddress{ pNestedPtr }, nestedProp, childDepth, rootRowID, HashUtil::combine( parentKey, static_cast<uint64>( nestedProp._name.getHash() ) ),
                                PropertyRowChild::Field, bDisabled || editState == PropertyEditState::Disabled, nullptr );
                setParent( childRowID );
                _listBinding[childRowID]._parentRowID = rowID;
            }, true ); // 구조체의 기반 필드도 그린다(컴포넌트와 같은 규칙)
            return;
        }

        void* const pContainer = prop.getRawPtr( instance.get() );
        if ( pContainer == nullptr )
            return;
        // 원소 위젯도 같은 규칙을 받아야 한다. `ReadOnly` 가 단추만 가리면 `ReadOnly` 컨테이너의 원소가 그대로 편집된다.
        const bool bElementDisabled = bDisabled || EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true ) == false;

        const ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
        if ( pSeq == nullptr )
        {
            const IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
            if ( pMap == nullptr )
                return;
            // 값은 맵 노드 안에서 바로 고친다(값 프로퍼티는 오프셋 0 이라 값 주소가 곧 객체 주소다). 지우기는 맵 행 번호로 부른다.
            const PropertyInfo& valueProp = appendElementProperty( prop );
            uint32              ordinal   = 0;
            pMap->forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* pKey, void* pValue )
            {
                const uint32 childRowID = outState._rowCount;
                appendValueRow( outState, MutableObjectAddress{ pValue }, valueProp, childDepth, rootRowID, HashUtil::combine( parentKey, static_cast<uint64>( ordinal ) ),
                                PropertyRowChild::MapEntry, bElementDisabled, nullptr );
                PropertyValueRow& entry               = outState._listRow[childRowID];
                entry._parentRowID                    = rowID;
                entry._ordinal                        = ordinal;
                entry._label                          = Internal::formatValueText( pKey, prop._keyTypeName );
                entry._bRemovable                     = bReadOnly ? SW_FALSE : SW_TRUE;
                _listBinding[childRowID]._parentRowID = rowID;
                _listBinding[childRowID]._ordinal     = ordinal;
                ++ordinal;
            } ) );
            return;
        }

        const size_t count = pSeq->getSize( pContainer );
        if ( pSeq->allowsInPlaceElementWrite() == false )
        {
            // 원소는 글 입력란이다. 고치면 컨테이너 행 번호로 지우고 다시 넣는다(원소가 곧 정렬 키라 제자리에 쓰면 트리가 깨진다).
            for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
            {
                PropertyValueRow& element = appendRow( outState, PropertyRowKind::KeyedElement, childDepth, HashUtil::combine( parentKey, static_cast<uint64>( elemIndex ) ) );
                RowBinding&       bind    = _listBinding.back();
                bind._instance            = instance;
                bind._pProp               = &prop;
                bind._rootRowID           = rootRowID;
                bind._parentRowID         = rowID; // 값 자리가 컨테이너 행과 같다 — 경로 마디가 없다(`_child` 는 None)
                element._parentRowID      = rowID;
                element._ordinal          = static_cast<uint32>( elemIndex );
                element._valueText        = Internal::formatValueText( pSeq->getElementConst( pContainer, elemIndex ), prop._elementTypeName );
                element._typeName         = prop._elementTypeName;
                element._propName         = prop._name;
                element._bRemovable       = bReadOnly ? SW_FALSE : SW_TRUE;
                element._bReadOnly        = bElementDisabled ? SW_TRUE : SW_FALSE;
                element._bDisabled        = bElementDisabled ? SW_TRUE : SW_FALSE;
            }
            return;
        }

        const PropertyInfo& elemProp = appendElementProperty( prop );
        for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
        {
            void* pElem = pSeq->getElement( pContainer, elemIndex );
            if ( pElem == nullptr )
                continue;
            // 원소 프로퍼티는 오프셋 0 이라 원소의 값 주소가 곧 그 프로퍼티를 가진 객체 주소다.
            const uint32 childRowID = outState._rowCount;
            appendValueRow( outState, MutableObjectAddress{ pElem }, elemProp, childDepth, rootRowID, HashUtil::combine( parentKey, static_cast<uint64>( elemIndex ) ), PropertyRowChild::Element,
                            bElementDisabled, nullptr );
            PropertyValueRow&                      element = outState._listRow[childRowID];
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "[%#]", elemIndex );
            element._label                        = label.c_str();
            element._parentRowID                  = rowID;
            element._ordinal                      = static_cast<uint32>( elemIndex );
            _listBinding[childRowID]._parentRowID = rowID;
            _listBinding[childRowID]._ordinal     = static_cast<uint32>( elemIndex );
        }
    }

    void PropertyGridViewModel::fillBuiltinRow( PropertyValueRow& row, const MutableObjectAddress instance, const PropertyInfo& prop, const InspectorBuiltinValue& builtin,
                                                const InspectorPropertyDisplay* pDisplay )
    {
        // 비트필드는 포인터를 잡을 수 없으므로 값으로 읽는다. 정수 계열과 bool 이 같은 체크박스를 쓴다.
        if ( prop._bIsBitField == SW_TRUE )
        {
            const bool bValue = prop.getValue<bool>( instance.get() );
            row._kind         = PropertyRowKind::BitField;
            row._value        = TaskValue{ bValue };
            row._valueText    = bValue ? "true" : "false";
            return;
        }
        const void* pValue = prop.getValuePtr<void>( instance.get() );
        if ( pValue == nullptr )
            return;
        row._kind         = PropertyRowKind::Builtin;
        row._widget       = builtin._widget;
        row._builtinIndex = builtin._index;
        row._value        = builtin._pReadValue( pValue );

        const InspectorDisplayUnit unit = pDisplay != nullptr ? pDisplay->_unit : InspectorPropertyLayout::getDisplayUnit( prop );
        row._scale                      = unit._scale;
        row._dragSpeed                  = unit._dragSpeed;
        if ( builtin._widget == InspectorValueWidget::Number )
            row._range = InspectorPropertyLayout::getNumericRange( prop, ObjectAddress{ instance } ); // 필드 이름 형태 경계는 이 객체의 그 필드 값이다
        else if ( builtin._widget == InspectorValueWidget::Vector3 || builtin._widget == InspectorValueWidget::Vector4 )
        {
            row._bColor    = InspectorPropertyLayout::isColorRequested( prop ) ? SW_TRUE : SW_FALSE;
            row._bColorHDR = prop._metadata->getEditor()._bColorHDR != SW_FALSE ? SW_TRUE : SW_FALSE;
        }
        else if ( builtin._widget == InspectorValueWidget::Text || builtin._widget == InspectorValueWidget::Name )
        {
            row._bAssetPath = ( prop._metadata->_bAssetPath != SW_FALSE || prop._metadata->_assetType.empty() == false ) ? SW_TRUE : SW_FALSE;
            row._bMultiline = prop._metadata->getEditor()._bMultiline != SW_FALSE ? SW_TRUE : SW_FALSE;
            if ( const string* pNameSource = prop.findCustomMeta( hashed_string( "NameSource" ) ); pNameSource != nullptr && builtin._widget == InspectorValueWidget::Name )
                row._nameSource = hashed_string( pNameSource->c_str() );
        }

        // 대상 타입이 있는 핸들(`TComponentHandle<T>`)은 선택기만 그린다 — 쓰는 값은 열거한 컴포넌트의 `getHandle()` 뿐이다.
        const bool bTypedHandle = static_cast<ReflectBuiltinIndex>( builtin._index ) == ReflectBuiltinOf<ComponentHandle>::kIndex && prop._handleTargetTypeName.empty() == false;
        if ( bTypedHandle )
        {
            row._bHandlePicker        = SW_TRUE;
            row._handleTargetTypeName = prop._handleTargetTypeName;
            if ( describeComponentHandle( prop._handleTargetTypeName, *static_cast<const ComponentHandle*>( pValue ), row._valueText, row._handleState ) == false )
            {
                // 활성 씬이나 모든 컴포넌트로 넓히는 것은 폴백이다 — 오류 글과 한 번의 로그로 멈춘다.
                row._bHandleUnavailable = SW_TRUE;
                if ( _setReportedHandle.insert( row._key ).second )
                    SW_LOG_ERROR_ID( "PropertyGridViewModel.InspectorComponentHandleCannot", "Inspector: component handle '%#' cannot be edited: %#", prop._name.c_str(), row._valueText.c_str() );
            }
            return;
        }
        if ( row._bReadOnly != SW_FALSE )
            PropertyGridViewModelInternal::formatReadOnlyBuiltin( builtin, pValue, row._scale, row._valueText );
    }

    const PropertyInfo& PropertyGridViewModel::appendElementProperty( const PropertyInfo& containerProp )
    {
        _listElementProp.push_back( make_unique<PropertyInfo>( InspectorPropertyLayout::makeElementProperty( containerProp ) ) );
        return *_listElementProp.back();
    }

    bool PropertyGridViewModel::findRowValue( uint32 rowID, MutableObjectAddress& outInstance, const PropertyInfo*& outProp ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return false;
        outInstance = pBinding->_instance;
        outProp     = pBinding->_pProp;
        return true;
    }

    const PropertyGridViewModel::RowBinding* PropertyGridViewModel::findBinding( uint32 rowID ) const
    {
        return rowID < _listBinding.size() ? &_listBinding[rowID] : nullptr;
    }

    const PropertyGridViewModel::RowBinding* PropertyGridViewModel::findValueBinding( uint32 rowID ) const
    {
        const RowBinding* pBinding = findBinding( rowID );
        return ( pBinding != nullptr && pBinding->_pProp != nullptr && pBinding->_instance.get() != nullptr ) ? pBinding : nullptr;
    }

    const utf8* PropertyGridViewModel::getUnitNumberFormat( uint32 rowID, const utf8* pNumberFormat )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return pNumberFormat;
        InspectorPropertyDisplay* const pDisplay = pBinding->_pDisplay;
        if ( pDisplay == nullptr )
        {
            // 묶음 밖(컨테이너 원소 · 맵 값 · 중첩 필드)은 표시 정보가 없다 — 부를 때마다 짓는다.
            _scratchUnitFormat = InspectorPropertyLayout::appendUnitSuffix( pNumberFormat, InspectorPropertyLayout::getDisplayUnit( *pBinding->_pProp )._suffix );
            return _scratchUnitFormat.c_str();
        }
        if ( pDisplay->_pUnitFormatBase != pNumberFormat )
        {
            pDisplay->_unitFormat      = InspectorPropertyLayout::appendUnitSuffix( pNumberFormat, pDisplay->_unit._suffix );
            pDisplay->_pUnitFormatBase = pNumberFormat;
        }
        return pDisplay->_unitFormat.c_str();
    }

    bool PropertyGridViewModel::isExpanded( uint64 key, bool bDefaultOpen ) const
    {
        return ( _setFlippedKey.find( key ) != _setFlippedKey.end() ) != bDefaultOpen;
    }

    void PropertyGridViewModel::storeExpanded( uint64 key, bool bDefaultOpen, bool bExpanded )
    {
        if ( bExpanded == bDefaultOpen )
            _setFlippedKey.erase( key );
        else
            _setFlippedKey.insert( key );
    }

    float32 PropertyGridViewModel::findRowHeight( uint64 key, float32 defaultHeight ) const
    {
        const auto it = _mapRowHeight.find( key );
        return it != _mapRowHeight.end() ? it->second : defaultHeight;
    }

    void PropertyGridViewModel::setRowExpanded( uint32 rowID, bool bExpanded )
    {
        // 보기만 바꾼다 — 되돌리기 · 씬 dirty 에 남지 않는다.
        const RowBinding* pBinding = findBinding( rowID );
        if ( pBinding != nullptr )
            storeExpanded( pBinding->_key, pBinding->_bDefaultOpen != 0, bExpanded );
    }

    void PropertyGridViewModel::setRowHeight( uint32 rowID, float32 height )
    {
        // 보기만 바꾼다. 키는 대상 · 프로퍼티마다라 선택을 오가면 쌓인다 — 넘으면 비우고 다시 잰다(한 프레임 자리 셈이 기본 높이로 돌아갈 뿐이다).
        constexpr size_t  kMaxHeightCount = 8192;
        const RowBinding* pBinding        = findBinding( rowID );
        if ( pBinding == nullptr || height <= 0.0f )
            return;
        if ( _mapRowHeight.size() >= kMaxHeightCount )
            _mapRowHeight.clear();
        _mapRowHeight[pBinding->_key] = height;
    }

    // ------------------------------------------------------------------------------
    // 즐겨찾기 · 거르기
    // ------------------------------------------------------------------------------
    bool PropertyGridViewModel::isFavorite( string_view favoriteKey ) const
    {
        return _pFavorites != nullptr && _pFavorites->contains( favoriteKey );
    }

    void PropertyGridViewModel::keepFavoriteProperties( const TypeInfo& type, vector<InspectorPropertyGroup>& inoutListGroup ) const
    {
        InspectorPropertyGroup favoriteGroup;
        favoriteGroup._category = type.getDisplayName();
        for ( const InspectorPropertyGroup& group : inoutListGroup )
        {
            for ( const InspectorPropertyItem& item : group._listItem )
            {
                if ( isFavorite( item._pDisplay->_favoriteKey ) )
                    favoriteGroup._listItem.push_back( item );
            }
        }
        inoutListGroup.clear();
        if ( favoriteGroup._listItem.empty() == false )
            inoutListGroup.push_back( std::move( favoriteGroup ) );
    }

    void PropertyGridViewModel::toggleFavorite( string_view favoriteKey )
    {
        if ( _pFavorites == nullptr )
            return;
        (void)_pFavorites->toggle( favoriteKey );
        (void)_pFavorites->saveToFile( _pFavorites->getFilePath() ); // 실패는 saveToFile 이 경고한다
    }

    bool PropertyGridViewModel::isActionFavorite( const TypeInfo& type, const FunctionInfo& method ) const
    {
        return _pActionFavorites != nullptr && _pActionFavorites->contains( InspectorFavorites::makeMethodKey( type, method ) );
    }

    void PropertyGridViewModel::toggleActionFavorite( const TypeInfo& type, const FunctionInfo& method )
    {
        if ( _pActionFavorites == nullptr )
            return;
        (void)_pActionFavorites->toggle( InspectorFavorites::makeMethodKey( type, method ) );
        (void)_pActionFavorites->saveToFile( _pActionFavorites->getFilePath() ); // 실패는 saveToFile 이 경고한다
    }

    void PropertyGridViewModel::removeDefaultValued( const ObjectAddress instance, const ObjectAddress defaultInstance, vector<InspectorPropertyGroup>& inoutListGroup )
    {
        for ( InspectorPropertyGroup& group : inoutListGroup )
        {
            VectorUtil::removeIf( group._listItem, [this, instance, defaultInstance]( const InspectorPropertyItem& item )
            { return isMixed( *item._pProperty ) == false && EditorDefaultObjects::isDefaultValue( *item._pProperty, instance, defaultInstance ); } );
        }
        VectorUtil::removeIf( inoutListGroup, []( const InspectorPropertyGroup& group )
        { return group._listItem.empty(); } );
    }

    // ------------------------------------------------------------------------------
    // 명령
    // ------------------------------------------------------------------------------
    void PropertyGridViewModel::notifyRowEdited( uint32 rowID )
    {
        const RowBinding* pBinding = findBinding( rowID );
        if ( pBinding == nullptr )
            return;
        // 중첩 필드 · 원소의 편집도 대상에는 깊이 0 프로퍼티의 편집이다(다중 선택의 `_onEdited` 는 그 프로퍼티를 통째로 나머지에 입힌다).
        const RowBinding* pRoot = findValueBinding( pBinding->_rootRowID );
        if ( pRoot != nullptr )
            notifyPropertyEdited( *pRoot->_pProp );
    }

    void PropertyGridViewModel::notifyPropertyEdited( const PropertyInfo& prop )
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

    void PropertyGridViewModel::setRowValue( uint32 rowID, const TaskValue& value )
    {
        PropertyEditRequest request;
        request._kind  = PropertyEditKind::SetValue;
        request._value = value;
        submitEdit( rowID, std::move( request ), {} );
    }

    void PropertyGridViewModel::setRowNumber( uint32 rowID, float64 storedValue )
    {
        PropertyEditRequest request;
        request._kind   = PropertyEditKind::SetNumber;
        request._number = storedValue;
        submitEdit( rowID, std::move( request ), {} );
    }

    void PropertyGridViewModel::setPropertyText( uint32 rowID, string_view text, const utf8* pUndoLabel )
    {
        // 다중 편집 대상은 적용이 깊이 0 프로퍼티 값 글을 나머지에도 입힌다 — 구간이 편집이 닿는 컴포넌트 전부를 찍으므로 되돌리기가 나머지도 돌린다.
        PropertyEditRequest request;
        request._kind = PropertyEditKind::SetText;
        request._text = string{ text };
        submitEdit( rowID, std::move( request ), pUndoLabel != nullptr ? string_view{ pUndoLabel } : string_view{} );
    }

    bool PropertyGridViewModel::setAssetPath( uint32 rowID, string_view path )
    {
        // 받는 종류가 아니면(없는 행 포함) 쓰지 않는다 — `AssetRef<T>` 는 대상 종류(언리얼 `TSoftObjectPtr<T>`), 경로 칸은 `FileFilter`.
        const bool bAccepted = canAcceptAssetPath( rowID, path );
        if ( bAccepted )
            setPropertyText( rowID, path, "Set Asset" );
        return bAccepted;
    }

    bool PropertyGridViewModel::canAcceptAssetPath( uint32 rowID, string_view path ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return false;
        const PropertyInfo& prop = *pBinding->_pProp;
        if ( prop._typeName == hashed_string( AssetRefBase::kTypeName ) )
        {
            const AssetRefBase* pRef = prop.getValuePtr<AssetRefBase>( pBinding->_instance.get() );
            return pRef != nullptr && AssetRefBase::isPathOfTarget( path, pRef->getTargetType() );
        }
        return InspectorPropertyLayout::matchesFileFilter( prop._metadata->getEditor()._fileFilter, path );
    }

    bool PropertyGridViewModel::canPasteText( uint32 rowID, string_view text ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || text.empty() )
            return false;
        const PropertyInfo& prop = *pBinding->_pProp;
        // 타입 핸들은 선택기만이 입력 길이다(행의 `_bPasteAllowed` 와 같은 판정).
        if ( prop._handleTargetTypeName.empty() == false )
            return false;
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        if ( const EnumInfo* pEnumInfo = prop._containerWrapper == nullptr ? getTypeRegistry().findEnum( prop._typeName ) : nullptr )
        {
            // 판정은 이름 읽기만 한다 — 적용 길(`parseTextValue`)은 모르는 이름을 경고로 남기므로, 클립보드를 칸마다 재 보는 판정이 경고를 쏟지 않게 한다.
            const bool bQuoted     = text.size() >= 2 && text.front() == '"' && text.back() == '"'; // 플래그 조합은 따옴표로 복사된다(`parseTextValue` 와 같은 규칙)
            int64      parsedValue = 0;
            return pEnumInfo->tryParseText( bQuoted ? text.substr( 1, text.size() - 2 ) : text, parsedValue );
        }
        const InspectorBuiltinValue* pBuiltin = prop._containerWrapper == nullptr ? InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() ) : nullptr;
        if ( pBuiltin == nullptr )
            return true; // 적용이 거른다 — 읽지 못하면 값은 그대로다(`SerializerUtil::applyPropertyText`)
        if ( pBuiltin->_widget == InspectorValueWidget::Text || pBuiltin->_widget == InspectorValueWidget::Name )
            return true; // 글 칸은 어떤 글이든 받는다
        // 그 C++ 타입의 기본값 사본에 읽어 본다. 사본은 이 함수의 지역 값이라 그 저장 공간에 써도 된다.
        TaskValue scratch = pBuiltin->_pMakeDefault();
        void*     pValue  = const_cast<void*>( pBuiltin->_pFindTaskValue( scratch ) );
        return pValue != nullptr && SerializerUtil::parseTextValue( MutableValueAddress{ pValue }, prop._typeName, text, context );
    }

    bool PropertyGridViewModel::findObjectComponent( const hashed_string& targetTypeName, uint64 objectID, ComponentHandle& outHandle ) const
    {
        const TypeInfo*          pTargetType = InspectorComponentChoice::findTargetType( getTypeRegistry(), targetTypeName );
        const GameObjectManager* pManager    = findEditOwnerManager();
        const GameObject*        pObject     = ( pTargetType != nullptr && pManager != nullptr ) ? pManager->findGameObjectByID( objectID ) : nullptr;
        if ( pObject == nullptr )
            return false;
        // `forEachComponentOfType<Component>` 은 기반 타입이라 거르지 않는다 — 선택기(`InspectorComponentChoice::computeHandleState`)와 같은 파생 판정을 쓴다.
        bool bFound = false;
        pObject->forEachComponent( [pTargetType, &bFound, &outHandle]( const Component* pComponent )
        {
            const TypeInfo* pType = pComponent->findCachedTypeInfo();
            if ( bFound || pType == nullptr || pType->isDerivedFrom( pTargetType ) == false )
                return;
            outHandle = pComponent->getHandle();
            bFound    = true;
        } );
        return bFound;
    }

    void PropertyGridViewModel::resetToDefault( uint32 rowID )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || _pType == nullptr )
            return;
        const PropertyInfo& prop            = *pBinding->_pProp;
        const ObjectAddress defaultInstance = findDefaultInstance( pBinding->_instance.get(), *_pType );
        const string        text            = defaultInstance.get() != nullptr ? SerializerUtil::formatPropertyText( prop, defaultInstance, SerializeContext::getDefault( SerializePurpose::Authoring ) )
                                                                               : prop._metadata->_defaultValue;
        setPropertyText( rowID, text, "Reset to Default" );
    }

    string PropertyGridViewModel::formatPropertyText( uint32 rowID ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return string{};
        return SerializerUtil::formatPropertyText( *pBinding->_pProp, pBinding->_instance, SerializeContext::getDefault( SerializePurpose::Authoring ) );
    }

    void PropertyGridViewModel::setEnumValue( uint32 rowID, int64 value )
    {
        // 콤보에서 고른 값은 고르는 순간 바뀐다 — 고른 항목은 팝업 창의 다른 위젯이라 한 번 쓰기 구간이 된다(다음 마감에 오른다).
        PropertyEditRequest request;
        request._kind    = PropertyEditKind::SetEnum;
        request._integer = value;
        submitEdit( rowID, std::move( request ), {} );
    }

    void PropertyGridViewModel::submitContainerEdit( uint32 rowID, PropertyEditKind kind, uint32 ordinal, string_view text, string_view undoLabel )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || pBinding->_pProp->_containerWrapper == nullptr )
            return;
        PropertyEditRequest request;
        request._kind    = kind;
        request._ordinal = ordinal;
        request._text    = string{ text };
        submitEdit( rowID, std::move( request ), undoLabel );
    }

    void PropertyGridViewModel::addElement( uint32 rowID )
    {
        submitContainerEdit( rowID, PropertyEditKind::AddElement, 0, {}, "Add Element" );
    }

    void PropertyGridViewModel::clearElements( uint32 rowID )
    {
        submitContainerEdit( rowID, PropertyEditKind::ClearElements, 0, {}, "Clear Elements" );
    }

    void PropertyGridViewModel::removeMapEntry( uint32 rowID, size_t ordinal )
    {
        submitContainerEdit( rowID, PropertyEditKind::RemoveMapEntry, static_cast<uint32>( ordinal ), {}, "Remove Map Entry" );
    }

    void PropertyGridViewModel::addMapEntry( uint32 rowID, string_view keyText )
    {
        submitContainerEdit( rowID, PropertyEditKind::AddMapEntry, 0, keyText, "Add Map Entry" );
    }

    void PropertyGridViewModel::replaceKeyedElement( uint32 rowID, size_t index, string_view text )
    {
        submitContainerEdit( rowID, PropertyEditKind::ReplaceKeyedElement, static_cast<uint32>( index ), text, "Edit Set Element" );
    }

    void PropertyGridViewModel::removeKeyedElement( uint32 rowID, size_t index )
    {
        submitContainerEdit( rowID, PropertyEditKind::RemoveKeyedElement, static_cast<uint32>( index ), {}, "Remove Set Element" );
    }

    void PropertyGridViewModel::addKeyedElement( uint32 rowID, string_view text )
    {
        submitContainerEdit( rowID, PropertyEditKind::AddKeyedElement, 0, text, "Add Set Element" );
    }

    bool PropertyGridViewModel::buildEditPath( uint32 rowID, PropertyEditPath& outPath ) const
    {
        outPath._listNode.clear();
        uint32 current = rowID;
        for ( ;; )
        {
            const RowBinding* pBinding = findValueBinding( current );
            if ( pBinding == nullptr )
                return false;
            if ( pBinding->_parentRowID == current )
            {
                outPath._root = pBinding->_pProp->_name; // 깊이 0 행
                break;
            }
            PropertyPathNode node{};
            switch ( pBinding->_child )
            {
                case PropertyRowChild::Field:
                    node._step = PropertyPathStep::Field;
                    node._name = pBinding->_pProp->_name;
                    outPath._listNode.push_back( node );
                    break;
                case PropertyRowChild::Element:
                    node._step    = PropertyPathStep::Element;
                    node._ordinal = pBinding->_ordinal;
                    outPath._listNode.push_back( node );
                    break;
                case PropertyRowChild::MapEntry:
                    node._step    = PropertyPathStep::MapValue;
                    node._ordinal = pBinding->_ordinal;
                    outPath._listNode.push_back( node );
                    break;
                case PropertyRowChild::None:
                    break; // 원소가 곧 키인 시퀀스의 원소 행 — 값 자리가 부모(컨테이너 행)와 같다
            }
            current = pBinding->_parentRowID;
        }
        VectorUtil::reverse( outPath._listNode );
        return true;
    }

    void PropertyGridViewModel::submitEdit( uint32 rowID, PropertyEditRequest&& request, string_view undoLabel )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || _pType == nullptr || buildEditPath( rowID, request._path ) == false )
            return;
        if ( _pObject == nullptr )
        {
            // 씬 밖 대상(환경설정 · 문서) — 주인 패널이 인스턴스 수명을 든다. 지금 적용하고 그 패널에 알린다.
            if ( request.applyTo( MutableObjectAddress{ _pInstance }, *_pType, getTypeRegistry() ) )
                notifyRowEdited( rowID );
            return;
        }
        // 씬 대상 — UI 프레임은 씬 메모리에 쓰지 않는다. 편집 구간은 지금 연다(쓰기 전 값 — 앞 요청은 지난 게임 단계가 이미 적용했다).
        ensureEditSpan( *pBinding, undoLabel );
        vector<PropertyEditTarget> listOther;
        uint32                     sceneKey = 0;
        collectOtherEditTargets( listOther, sceneKey );
        PropertyGridViewModelInternal::requestSceneEdit( _targetScope, _targetObjectID, _targetComponentID, sceneKey, std::move( listOther ), std::move( request ) );
    }

    void PropertyGridViewModel::collectOtherEditTargets( vector<PropertyEditTarget>& outListOther, uint32& outSceneKey ) const
    {
        if ( _pObject == nullptr || _collectEditTargets.isBound() == false )
            return;
        outSceneKey = PropertyEditTarget::findSceneKey( *_pObject );
        vector<Component*> listComponent;
        _collectEditTargets( listComponent );
        for ( const Component* pEdited : listComponent )
        {
            if ( pEdited != nullptr && pEdited != _pComponent && pEdited->getOwner() != nullptr )
                outListOther.push_back( PropertyEditTarget::make( *pEdited->getOwner(), pEdited ) );
        }
    }

    void PropertyGridViewModel::ensureEditSpan( const RowBinding& binding, string_view label )
    {
        if ( _pObject == nullptr || EditorPlaySession::getUIState().areSceneEditsAllowed() == false )
            return;
        // 구간 단위는 깊이 0 프로퍼티다 — 중첩 필드 · 원소 편집도 다중 선택은 그 프로퍼티를 통째로 입힌다(`notifyRowEdited`).
        const RowBinding* pRoot = findValueBinding( binding._rootRowID );
        if ( pRoot == nullptr )
            return;
        const PropertyInfo&          rootProp = *pRoot->_pProp;
        const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( rootProp._typeName.c_str() );
        if ( pBuiltin != nullptr && static_cast<ReflectBuiltinIndex>( pBuiltin->_index ) == ReflectBuiltinIndex::katomic_bool )
            return; // 다른 스레드가 쓰는 값이라 되돌리기에 넣지 않는다(되돌리면 런타임이 쓴 값을 덮는다)
        if ( _editSpans.isHeld( pRoot->_key ) )
            return; // 끄는 중 — 연 때 찍은 값이 이 구간의 "편집 전" 이다
        const string undoLabel = label.empty() ? InspectorPropertyLayout::makeUndoLabel( InspectorPropertyLayout::getPropertyLabel( rootProp ).c_str() ) : string{ label };
        _editSpans.open( pRoot->_key, PropertyEditSpan::captureProperty( *_pObject, _pComponent, rootProp, _collectEditTargets, undoLabel ) );
    }

    void PropertyGridViewModel::touchRow( uint32 rowID )
    {
        const RowBinding* pBinding = findBinding( rowID );
        const RowBinding* pRoot    = pBinding != nullptr ? findBinding( pBinding->_rootRowID ) : nullptr;
        if ( pRoot != nullptr )
            _editSpans.touch( pRoot->_key );
    }

    void PropertyGridViewModel::trackComponentRegion( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, bool bActive, string_view label )
    {
        if ( bActive == false || EditorPlaySession::getUIState().areSceneEditsAllowed() == false )
            return;
        // 행 키(`_key`)와 겹치지 않게 영역 씨앗을 섞는다.
        static constexpr uint64 kRegionSeed = 0x5245474E494F4E00ull;
        const uint64            key         = HashUtil::combine( HashUtil::combine( HashUtil::combine( kRegionSeed, static_cast<uint64>( scope ) ), objectID ), componentID );
        if ( _editSpans.isHeld( key ) )
        {
            _editSpans.touch( key );
            return;
        }
        GameObject* pObject    = findSceneObject( scope, objectID );
        Component*  pComponent = pObject != nullptr ? pObject->findComponentByID( componentID ) : nullptr;
        if ( pComponent == nullptr )
            return;
        const string undoLabel = InspectorPropertyLayout::makeUndoLabel( string{ label }.c_str() );
        _editSpans.open( key, PropertyEditSpan::captureComponent( *pComponent, undoLabel ) );
        _editSpans.touch( key );
    }

    void PropertyGridViewModel::endFrame()
    {
        vector<PropertyEditSpan> listClosed;
        _editSpans.collectClosed( listClosed );
        if ( listClosed.empty() )
            return;
        // 마감도 게임 단계다 — 이번 프레임의 쓰기 요청 뒤에 서서(요청 순서) 쓰기가 적용된 다음 지금 값과 견준다. 끌기 한 번이 되돌리기 한 단계다.
        EditorCommandRegistry::getInEditorContext().requestSceneEdit( SW_DELEGATE_LAMBDA( Delegate<void( EditorTransaction& )>, [listClosed]( EditorTransaction& transaction )
        {
            vector<PropertyEditSpan> listSpan = listClosed;
            PropertyEditSpan::commit( transaction, listSpan );
        } ) );
    }

    void PropertyGridViewModel::invokeMethod( const FunctionInfo& method, const vector<ReflectedValue>& listArg )
    {
        if ( _pInstance == nullptr || _pType == nullptr )
            return;
        if ( _pObject == nullptr )
        {
            // 씬 밖 대상(환경설정 구조체 등) — 주인 패널이 수명을 들고 되돌리기 대상이 없다. 지금 부른다.
            _pLastInvokeResult->begin( method._name );
            ReflectedValue          returnValue;
            const ReflectCallResult result = ReflectionInvoke::callByName( *_pType, _pInstance, method._hashName, listArg, &returnValue );
            _pLastInvokeResult->record( result, returnValue );
            return;
        }
        // 씬 대상 — UI 프레임은 씬을 바꾸는 호출을 하지 않는다. 게임 단계가 부르고 결과 칸에 둔다.
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "Call %#", method._name.c_str() );
        vector<PropertyEditTarget> listOther;
        uint32                     sceneKey = 0;
        collectOtherEditTargets( listOther, sceneKey );
        PropertyGridViewModelInternal::requestInvoke( _targetScope, _targetObjectID, _targetComponentID, sceneKey, std::move( listOther ), method._hashName,
                                                      string{ textBuilder.view() }, listArg, _pLastInvokeResult );
    }

    bool PropertyGridViewModel::isEventBound( const EventInfo& event ) const
    {
        return _pInstance != nullptr && ReflectionInvoke::isEventBound( event, _pInstance );
    }

    void PropertyGridViewModel::broadcastEvent( const EventInfo& event )
    {
        if ( _pInstance == nullptr )
            return;
        if ( _pObject == nullptr )
        {
            (void)ReflectionInvoke::broadcastEvent( event, _pInstance, {} ); // 씬 밖 대상 — 주인 패널이 수명을 든다
            return;
        }
        PropertyGridViewModelInternal::requestBroadcast( _targetScope, _targetObjectID, _targetComponentID, event._name );
    }

    // ------------------------------------------------------------------------------
    // 값 모음
    // ------------------------------------------------------------------------------
    void PropertyGridViewModel::collectAssetEntries( uint32 rowID, vector<EditorAssetIndexEntry>& outListEntry ) const
    {
        outListEntry.clear();
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        // 에디터의 에셋 색인에서 읽는다 — 팝업을 열 때마다 리소스 트리를 걷지 않는다.
        EditorAssetCatalog::collectEntriesOnUIThread( EditorPaths::getAssetPaths(), outListEntry );
        // 타입 있는 참조는 대상 종류가 거른다(언리얼 `TSoftObjectPtr<T>` 의 선택기와 같다). 그 밖의 경로 칸은 `FileFilter` 를 본다.
        const PropertyInfo& prop = *pBinding->_pProp;
        const AssetRefBase* pRef = prop._typeName == hashed_string( AssetRefBase::kTypeName ) ? prop.getValuePtr<AssetRefBase>( pBinding->_instance.get() ) : nullptr;
        const string_view   fileFilter{ prop._metadata->getEditor()._fileFilter };
        VectorUtil::removeIf( outListEntry, [pRef, fileFilter]( const EditorAssetIndexEntry& entry )
        { return ( pRef != nullptr ? AssetRefBase::isPathOfTarget( entry._path, pRef->getTargetType() ) : InspectorPropertyLayout::matchesFileFilter( fileFilter, entry._path ) ) == false; } );
    }

    bool PropertyGridViewModel::collectCatalogKeyEntries( uint32 rowID, vector<string>& outListID ) const
    {
        outListID.clear();
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return false;
        // 어느 카탈로그인지는 쿠커와 같은 표가 정한다(언리얼 `FDataTableRowHandle` 의 행 고르기가 표를 묻는 자리) — 코드가 따로 경로를 적지 않는다.
        DataCatalogReferenceRules rules;
        if ( rules.loadFromAsset() == false )
            return false;
        const DataCatalogKeyTarget* pTarget = rules.findKeyTarget( pBinding->_pProp->_metadata->_catalogKeyTag );
        if ( pTarget == nullptr )
            return false;
        vector<EditorAssetIndexEntry> listEntry;
        EditorAssetCatalog::collectEntriesOnUIThread( EditorPaths::getAssetPaths(), listEntry );
        vector<string> listAssetID;
        listAssetID.reserve( listEntry.size() );
        for ( EditorAssetIndexEntry& entry : listEntry )
        {
            listAssetID.push_back( std::move( entry._path ) );
        }
        // 실행의 작업 도메인(`scratch/` — 리소스 트리 밖이라 색인에 없다)도 마운트된 리소스다: 시나리오가 사본 폴더로 내보낸 프리셋을 고를 수 있어야 한다
        // (언리얼 임시 마운트 지점의 패키지도 에셋 레지스트리에 오른다). 폴더가 없으면 고를 것이 없다.
        vector<AbsolutePath> listScratchFile;
        const AbsolutePath&  scratchFolder = AssetPaths::getScratchFolderPath();
        if ( FileUtil::isDirectory( scratchFolder ) && EditorContentScan::collectFiles( scratchFolder, ".xml", listScratchFile, true ) )
        {
            for ( const AbsolutePath& filePath : listScratchFile )
            {
                listAssetID.push_back( EditorPaths::getAssetPaths().toAssetID( filePath ) );
            }
        }
        fixed_string<constant::buffer::kMax128> opening;
        formatstring( opening.data(), opening.capacity(), "<%#", pTarget->_catalog );
        fixed_string<constant::buffer::kMax128> entryOpening; // 항목 하나짜리 파일(`collectEntryIDs` 가 루트 이름으로 가린다)
        formatstring( entryOpening.data(), entryOpening.capacity(), "<%#", pTarget->_entry );
        for ( const string& assetID : listAssetID )
        {
            string text;
            if ( StringUtil::endsWith( assetID, ".xml", true ) == false || AssetIO::readTextAsset( AssetID{ assetID }, text ) == false ||
                 ( text.find( opening.view() ) == string::npos && text.find( entryOpening.view() ) == string::npos ) )
                continue;
            XMLDocument doc;
            if ( doc.parseData( text, assetID ) == false )
                continue;
            vector<string_view> listID; // 항목 경로(`Honor/Action`)는 쿠커와 같은 함수가 따라간다
            DataCatalogReferenceRules::collectEntryIDs( doc.getRoot(), *pTarget, listID );
            for ( const string_view id : listID )
            {
                outListID.emplace_back( id );
            }
        }
        VectorUtil::sort( outListID );
        outListID.erase( std::unique( outListID.begin(), outListID.end() ), outListID.end() );
        return true;
    }

    bool PropertyGridViewModel::describeComponentHandle( const hashed_string& targetTypeName, const ComponentHandle& handle, string& outPreview, InspectorHandleState& outState ) const
    {
        const TypeInfo*          pTargetType = InspectorComponentChoice::findTargetType( getTypeRegistry(), targetTypeName );
        const GameObjectManager* pManager    = findEditOwnerManager();
        if ( pTargetType == nullptr || pManager == nullptr )
        {
            fixed_string<constant::buffer::kMax512> text;
            if ( pTargetType == nullptr )
                formatstring( text.data(), text.capacity(), "(unknown component type '%#')", targetTypeName.c_str() );
            else
                formatstring( text.data(), text.capacity(), "(no scene: cannot pick a component)" );
            outPreview = text.c_str();
            return false;
        }
        outState   = InspectorComponentChoice::computeHandleState( *pManager, *pTargetType, handle );
        outPreview = InspectorComponentChoice::makePreview( *pManager, handle, outState );
        return true;
    }

    void PropertyGridViewModel::collectComponentChoices( const hashed_string& targetTypeName, vector<InspectorComponentChoice>& outListChoice ) const
    {
        outListChoice.clear();
        const TypeInfo*          pTargetType = InspectorComponentChoice::findTargetType( getTypeRegistry(), targetTypeName );
        const GameObjectManager* pManager    = findEditOwnerManager();
        if ( pTargetType != nullptr && pManager != nullptr )
            InspectorComponentChoice::collectChoices( *pManager, *pTargetType, outListChoice );
    }

    // ------------------------------------------------------------------------------
    // 창구
    // ------------------------------------------------------------------------------
    GameObject* PropertyGridViewModel::findSceneObject( EditorPropertyGridScope scope, uint64 objectID )
    {
        return PropertyGridViewModelInternal::findObject( scope, objectID );
    }

    PropertyDrawerRegistry& PropertyGridViewModel::getPropertyManager()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<PropertyDrawerRegistry>();
    }

    const TypeRegistry& PropertyGridViewModel::getTypeRegistry()
    {
        return editor::getSubsystem<TypeRegistry>();
    }
} // namespace sw::editor
