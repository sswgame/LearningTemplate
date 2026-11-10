#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/Model/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"
#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"
#include "Editor/Panels/Inspector/Model/InspectorObjectEdit.h"
#include "Editor/Panels/Inspector/Model/PropertyEditConditionExpr.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyGridModelInternal
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

            /** @brief 편집 하나입니다. 주인 오브젝트가 있으면 되돌리기에 남기고(`InspectorObjectEdit`), 없으면(씬 밖 객체) 그냥 바꿉니다. */
            template <typename EditFunc>
            static void applyEdit( GameObject* pOwner, string_view undoLabel, EditFunc&& edit )
            {
                if ( pOwner == nullptr )
                    edit();
                else
                    InspectorObjectEdit::applyObjectEdit( pOwner, undoLabel, std::forward<EditFunc>( edit ) );
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
                for ( const auto& [flag, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( flag != 0 && ( value & flag ) == flag )
                    {
                        if ( outText.empty() == false )
                            outText += " | ";
                        outText += nameHashed.c_str();
                    }
                }
                if ( outText.empty() )
                    outText = ( value == 0 ) ? "None" : "<Unknown>";
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

            /** @brief 맵 키 · 시퀀스 원소의 값 글입니다(값 주소 쪽 변환). */
            static string formatValueText( const void* pValue, const hashed_string& typeName )
            {
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
        , _listFilteredGroup{}
        , _listBinding{}
        , _listElementProp{}
        , _mapRowHeight{}
        , _setFlippedKey{}
        , _setReportedHandle{}
        , _targetSalt{ 0 }
        , _lastInvokeResult{}
    {
    }

    PropertyGridModel::~PropertyGridModel() = default;

    bool PropertyGridModel::beginTarget( const EditorPropertyGridTarget& target )
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
        _listBinding.clear();
        _listElementProp.clear();
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

    // ------------------------------------------------------------------------------
    // 값 행
    // ------------------------------------------------------------------------------
    void PropertyGridModel::gather( const PropertyGridGatherParams& params, PropertyGridViewState& outState )
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
        const float32 margin          = ( window._clipMaxY - window._clipMinY ) * PropertyGridModelInternal::kOffscreenMarginRatio;
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

            for ( const PropertyInfo* pProp : group._listProperty )
            {
                const PropertyInfo& prop = *pProp;
                // `EditCondition` 이 거짓이면 숨기거나(EditConditionHides) 막는다. 판정은 ImGui 에 의존하지 않는 `PropertyEditConditionExpr` 이 한다.
                const PropertyEditState editState = PropertyEditConditionExpr::getEditState( type, prop, instance );
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
                appendValueRow( outState, instance, prop, 0, constant::invalid_index::kUint32, rowKey, PropertyRowChild::None, editState == PropertyEditState::Disabled );
                // 깊이 0 행만의 값: 이름 칸(혼합 · 즐겨찾기 · 되돌리기 단추 · 표식). 자식이 행을 더했어도 번호로 다시 찾는다.
                PropertyValueRow& row = outState._listRow[rowID];
                row._markName         = type._name.c_str();
                row._markName += ".";
                row._markName += prop._name.c_str();
                row._bMixed          = isMixed( prop ) ? SW_TRUE : SW_FALSE;
                row._favoriteKey     = _layoutCache.getLaidOutDisplay( prop )._favoriteKey; // 묶음을 지을 때 만든 키
                row._bFavoriteTarget = bFavoriteTarget ? SW_TRUE : SW_FALSE;
                row._bFavorite       = ( bFavoriteTarget && isFavorite( row._favoriteKey ) ) ? SW_TRUE : SW_FALSE;
                row._bModified       = EditorDefaultObjects::isDefaultValue( prop, instance, defaultInstance ) ? SW_FALSE : SW_TRUE;
                row._bCanReset       = ( defaultInstance.get() != nullptr || prop._metadata->_defaultValue.empty() == false ) ? SW_TRUE : SW_FALSE;
                row._bTransient      = prop._metadata->_bTransient == SW_TRUE ? SW_TRUE : SW_FALSE;
                // 타입 핸들은 선택기만이 입력 길이다. 글로 붙이면 대상 타입을 확인하지 않은 값이 들어간다.
                row._bPasteAllowed = prop._handleTargetTypeName.empty() ? SW_TRUE : SW_FALSE;
            }
        }
    }

    PropertyValueRow& PropertyGridModel::appendRow( PropertyGridViewState& outState, PropertyRowKind kind, uint16 depth, uint64 key )
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
        binding._key       = key;
        binding._rootRowID = rowID;
        _listBinding.push_back( binding );
        if ( kind != PropertyRowKind::Category && kind != PropertyRowKind::Skipped )
            ++outState._filledCount;
        return row;
    }

    void PropertyGridModel::appendValueRow( PropertyGridViewState& outState, const MutableObjectAddress instance, const PropertyInfo& prop, uint16 depth, uint32 rootRowID, uint64 key,
                                            PropertyRowChild child, bool bDisabled )
    {
        using Internal = PropertyGridModelInternal;

        PropertyValueRow& row   = appendRow( outState, PropertyRowKind::Unreadable, depth, key );
        const uint32      rowID = row._rowID;
        RowBinding&       bind  = _listBinding.back();
        bind._instance          = instance;
        bind._pProp             = &prop;
        bind._rootRowID         = rootRowID == constant::invalid_index::kUint32 ? rowID : rootRowID;

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
            fillBuiltinRow( row, instance, prop, *pBuiltin );
            return;
        }
        if ( getPropertyManager().find( prop._typeName.c_str() ) != nullptr )
        {
            // 확장(`IInspectorProperty`)이 맡는 타입 — 값은 글로 넘긴다(확장이 자기 형식으로 읽는다).
            row._kind       = PropertyRowKind::Text;
            row._valueText  = SerializerUtil::formatPropertyText( prop, instance, SerializeContext::getDefault( SerializePurpose::Authoring ) );
            row._bAssetPath = ( prop._metadata->_bAssetPath != SW_FALSE || prop._metadata->_assetType.empty() == false ) ? SW_TRUE : SW_FALSE;
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
            for ( const auto& [value, nameHashed] : pEnumInfo->_mapValueToName )
            {
                row._listChoice.push_back( PropertyEnumChoice{ nameHashed, value } );
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
            ISequenceContainerWrapper*              pSeq = prop._containerWrapper->asSequence();
            if ( pSeq == nullptr )
            {
                IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
                if ( pMap == nullptr )
                {
                    row._valueText = Internal::makeNoInspectorText( prop );
                    return;
                }
                row._kind         = PropertyRowKind::Map;
                row._elementCount = static_cast<uint32>( pMap->getSize( pContainer ) );
                formatstring( header.data(), header.capacity(), "[%# -> %#] (%# entries)", prop._keyTypeName.c_str(), prop._elementTypeName.c_str(), row._elementCount );
                row._bCanAddRemove = bReadOnly ? SW_FALSE : SW_TRUE;
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

    void PropertyGridModel::appendChildRows( PropertyGridViewState& outState, uint32 rowID, const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        using Internal = PropertyGridModelInternal;

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
                const PropertyEditState editState = PropertyEditConditionExpr::getEditState( *pFieldType, nestedProp, ObjectAddress{ pNestedPtr } );
                if ( editState == PropertyEditState::Hidden )
                    return;
                const uint32 childRowID = outState._rowCount;
                appendValueRow( outState, MutableObjectAddress{ pNestedPtr }, nestedProp, childDepth, rootRowID, HashUtil::combine( parentKey, static_cast<uint64>( nestedProp._name.getHash() ) ),
                                PropertyRowChild::Field, bDisabled || editState == PropertyEditState::Disabled );
                setParent( childRowID );
            }, true ); // 구조체의 기반 필드도 그린다(컴포넌트와 같은 규칙)
            return;
        }

        void* const pContainer = prop.getRawPtr( instance.get() );
        if ( pContainer == nullptr )
            return;
        // 원소 위젯도 같은 규칙을 받아야 한다. `ReadOnly` 가 단추만 가리면 `ReadOnly` 컨테이너의 원소가 그대로 편집된다.
        const bool bElementDisabled = bDisabled || EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true ) == false;

        ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
        if ( pSeq == nullptr )
        {
            IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
            if ( pMap == nullptr )
                return;
            // 값은 맵 노드 안에서 바로 고친다(값 프로퍼티는 오프셋 0 이라 값 주소가 곧 객체 주소다). 지우기는 맵 행 번호로 부른다.
            const PropertyInfo& valueProp = appendElementProperty( prop );
            uint32              ordinal   = 0;
            pMap->forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* pKey, void* pValue )
            {
                const uint32 childRowID = outState._rowCount;
                appendValueRow( outState, MutableObjectAddress{ pValue }, valueProp, childDepth, rootRowID, HashUtil::combine( parentKey, static_cast<uint64>( ordinal ) ),
                                PropertyRowChild::MapEntry, bElementDisabled );
                PropertyValueRow& entry = outState._listRow[childRowID];
                entry._parentRowID      = rowID;
                entry._ordinal          = ordinal;
                entry._label            = Internal::formatValueText( pKey, prop._keyTypeName );
                entry._bRemovable       = bReadOnly ? SW_FALSE : SW_TRUE;
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
                            bElementDisabled );
            PropertyValueRow&                      element = outState._listRow[childRowID];
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "[%#]", elemIndex );
            element._label       = label.c_str();
            element._parentRowID = rowID;
            element._ordinal     = static_cast<uint32>( elemIndex );
        }
    }

    void PropertyGridModel::fillBuiltinRow( PropertyValueRow& row, const MutableObjectAddress instance, const PropertyInfo& prop, const InspectorBuiltinValue& builtin )
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

        const InspectorDisplayUnit& unit = getDisplayUnit( prop );
        row._scale                       = unit._scale;
        row._dragSpeed                   = unit._dragSpeed;
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
                    SW_LOG_ERROR_ID( "InspectorPropertyManager.InspectorComponentHandleCannot", "Inspector: component handle '%#' cannot be edited: %#", prop._name.c_str(), row._valueText.c_str() );
            }
            return;
        }
        if ( row._bReadOnly != SW_FALSE )
            PropertyGridModelInternal::formatReadOnlyBuiltin( builtin, pValue, row._scale, row._valueText );
    }

    const PropertyInfo& PropertyGridModel::appendElementProperty( const PropertyInfo& containerProp )
    {
        _listElementProp.push_back( make_unique<PropertyInfo>( InspectorPropertyLayout::makeElementProperty( containerProp ) ) );
        return *_listElementProp.back();
    }

    const InspectorDisplayUnit& PropertyGridModel::getDisplayUnit( const PropertyInfo& prop )
    {
        const InspectorPropertyDisplay* pDisplay = _layoutCache.findDisplay( prop );
        if ( pDisplay != nullptr )
            return pDisplay->_unit;
        // 묶음 밖 — 컨테이너 원소 · 맵 값 · 구조체 필드는 캐시에 없다.
        _scratchDisplay._unit = InspectorPropertyLayout::getDisplayUnit( prop );
        return _scratchDisplay._unit;
    }

    bool PropertyGridModel::findRowValue( uint32 rowID, MutableObjectAddress& outInstance, const PropertyInfo*& outProp ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return false;
        outInstance = pBinding->_instance;
        outProp     = pBinding->_pProp;
        return true;
    }

    const PropertyGridModel::RowBinding* PropertyGridModel::findBinding( uint32 rowID ) const
    {
        return rowID < _listBinding.size() ? &_listBinding[rowID] : nullptr;
    }

    const PropertyGridModel::RowBinding* PropertyGridModel::findValueBinding( uint32 rowID ) const
    {
        const RowBinding* pBinding = findBinding( rowID );
        return ( pBinding != nullptr && pBinding->_pProp != nullptr && pBinding->_instance.get() != nullptr ) ? pBinding : nullptr;
    }

    const utf8* PropertyGridModel::getUnitNumberFormat( uint32 rowID, const utf8* pNumberFormat )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return pNumberFormat;
        const PropertyInfo&       prop     = *pBinding->_pProp;
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

    bool PropertyGridModel::isExpanded( uint64 key, bool bDefaultOpen ) const
    {
        return ( _setFlippedKey.find( key ) != _setFlippedKey.end() ) != bDefaultOpen;
    }

    void PropertyGridModel::storeExpanded( uint64 key, bool bDefaultOpen, bool bExpanded )
    {
        if ( bExpanded == bDefaultOpen )
            _setFlippedKey.erase( key );
        else
            _setFlippedKey.insert( key );
    }

    float32 PropertyGridModel::findRowHeight( uint64 key, float32 defaultHeight ) const
    {
        const auto it = _mapRowHeight.find( key );
        return it != _mapRowHeight.end() ? it->second : defaultHeight;
    }

    void PropertyGridModel::setRowExpanded( uint32 rowID, bool bExpanded )
    {
        // 보기만 바꾼다 — 되돌리기 · 씬 dirty 에 남지 않는다.
        const RowBinding* pBinding = findBinding( rowID );
        if ( pBinding != nullptr )
            storeExpanded( pBinding->_key, pBinding->_bDefaultOpen != 0, bExpanded );
    }

    void PropertyGridModel::setRowHeight( uint32 rowID, float32 height )
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

    // ------------------------------------------------------------------------------
    // 명령
    // ------------------------------------------------------------------------------
    void PropertyGridModel::notifyRowEdited( uint32 rowID )
    {
        const RowBinding* pBinding = findBinding( rowID );
        if ( pBinding == nullptr )
            return;
        // 중첩 필드 · 원소의 편집도 대상에는 깊이 0 프로퍼티의 편집이다(다중 선택의 `_onEdited` 는 그 프로퍼티를 통째로 나머지에 입힌다).
        const RowBinding* pRoot = findValueBinding( pBinding->_rootRowID );
        if ( pRoot != nullptr )
            notifyPropertyEdited( *pRoot->_pProp );
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

    void PropertyGridModel::setRowValue( uint32 rowID, const TaskValue& value )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        const PropertyInfo& prop = *pBinding->_pProp;
        void* const         pObj = pBinding->_instance.get();
        if ( prop._bIsBitField == SW_TRUE )
        {
            const bool* pValue = value.getStoredSize() == sizeof( bool ) ? value.getPtr<bool>() : nullptr;
            if ( pValue == nullptr )
                return;
            prop.setValue<bool>( pObj, *pValue );
        }
        else
        {
            const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() );
            void*                        pMember  = prop.getValuePtr<void>( pObj );
            if ( pBuiltin == nullptr || pMember == nullptr || pBuiltin->_pWriteValue( value, pMember ) == false )
                return;
        }
        notifyRowEdited( rowID );
    }

    void PropertyGridModel::setRowNumber( uint32 rowID, float64 storedValue )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        const PropertyInfo&          prop     = *pBinding->_pProp;
        const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() );
        void*                        pMember  = prop.getValuePtr<void>( pBinding->_instance.get() );
        if ( pBuiltin == nullptr || pMember == nullptr || pBuiltin->_widget != InspectorValueWidget::Number )
            return;
        // 위젯은 `UIMin` · `UIMax` 안에서 움직이고, 값은 항상 `Min` · `Max` 로 제한한다(`InspectorNumericRange` — 직접 입력이 범위 밖일 수 있다).
        const InspectorNumericRange range   = InspectorPropertyLayout::getNumericRange( prop, ObjectAddress{ pBinding->_instance } );
        const float64               clamped = InspectorPropertyLayout::clampToAllowedRange( range, storedValue );
        if ( ReflectedValueUtil::writeNumber( static_cast<ReflectBuiltinIndex>( pBuiltin->_index ), pMember, clamped ) == false )
            return;
        notifyRowEdited( rowID );
    }

    void PropertyGridModel::setPropertyText( uint32 rowID, string_view text, const utf8* pUndoLabel )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        // 그 프로퍼티 하나만 쓰고(비트필드는 그 비트만) 알린 뒤 되돌리기에 남긴다. `{"이름":값}` 을 JSON 으로 읽히면 읽기에서 빠진
        // 프로퍼티마다 기본값을 채우므로 다른 프로퍼티까지 바뀐다. 그래서 텍스트 하나로 프로퍼티 하나만 쓴다.
        const PropertyInfo&        prop     = *pBinding->_pProp;
        const MutableObjectAddress instance = pBinding->_instance;
        const string               value{ text };
        auto                       apply = [this, &prop, instance, &value, rowID]()
        {
            if ( SerializerUtil::applyPropertyText( prop, instance, value, SerializeContext::getDefault( SerializePurpose::Authoring ) ) )
                notifyRowEdited( rowID );
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

    bool PropertyGridModel::setAssetPath( uint32 rowID, string_view path )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return false;
        const PropertyInfo& prop = *pBinding->_pProp;
        // 받는 종류가 아니면 쓰지 않는다 — `AssetRef<T>` 는 대상 종류(언리얼 `TSoftObjectPtr<T>`), 경로 칸은 `FileFilter`.
        bool bAccepted = false;
        if ( prop._typeName == hashed_string( AssetRefBase::kTypeName ) )
        {
            const AssetRefBase* pRef = prop.getValuePtr<AssetRefBase>( pBinding->_instance.get() );
            bAccepted                = pRef != nullptr && AssetRefBase::isPathOfTarget( path, pRef->getTargetType() );
        }
        else
            bAccepted = InspectorPropertyLayout::matchesFileFilter( prop._metadata->getEditor()._fileFilter, path );
        if ( bAccepted )
            setPropertyText( rowID, path, "Set Asset" );
        return bAccepted;
    }

    void PropertyGridModel::resetToDefault( uint32 rowID )
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

    string PropertyGridModel::formatPropertyText( uint32 rowID ) const
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return string{};
        return SerializerUtil::formatPropertyText( *pBinding->_pProp, pBinding->_instance, SerializeContext::getDefault( SerializePurpose::Authoring ) );
    }

    void PropertyGridModel::setEnumValue( uint32 rowID, int64 value )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        const PropertyInfo& prop      = *pBinding->_pProp;
        const EnumInfo*     pEnumInfo = getTypeRegistry().findEnum( prop._typeName );
        void*               pMemory   = prop.getValuePtr<void>( pBinding->_instance.get() );
        if ( pEnumInfo == nullptr || pMemory == nullptr )
            return;
        // 콤보에서 고른 값은 고르는 순간 바뀐다 — 콤보 단추의 활성화 · 해제로는 잡히지 않으므로 쓰기 직전에 편집 전을 찍어 남긴다.
        // enum 의 실제 크기로 쓴다. int32 로 쓰면 uint8 enum 뒤의 필드를 덮어쓴다(`CameraComponent::_role` 을 고르면 `_bOrthographic` 이 꺼졌다).
        InspectorEditRecorder::recordImmediateEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
        pEnumInfo->writeValueToMemory( pMemory, value );
        notifyRowEdited( rowID );
    }

    template <typename EditFunc>
    void PropertyGridModel::applyContainerEdit( uint32 rowID, string_view undoLabel, EditFunc&& edit )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || pBinding->_pProp->_containerWrapper == nullptr )
            return;
        const PropertyInfo& prop       = *pBinding->_pProp;
        void* const         pContainer = prop.getRawPtr( pBinding->_instance.get() );
        if ( pContainer == nullptr )
            return;
        PropertyGridModelInternal::applyEdit( _pObject, undoLabel, [&edit, &prop, pContainer]()
        { edit( prop, pContainer ); } );
        notifyRowEdited( rowID );
    }

    void PropertyGridModel::addElement( uint32 rowID )
    {
        applyContainerEdit( rowID, "Add Element", []( const PropertyInfo& prop, void* pContainer )
        {
            if ( ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence() )
                pSeq->addElementDefault( pContainer );
        } );
    }

    void PropertyGridModel::clearElements( uint32 rowID )
    {
        applyContainerEdit( rowID, "Clear Elements", []( const PropertyInfo& prop, void* pContainer )
        {
            if ( ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence() )
                pSeq->clear( pContainer );
        } );
    }

    void PropertyGridModel::removeMapEntry( uint32 rowID, size_t ordinal )
    {
        applyContainerEdit( rowID, "Remove Map Entry", [ordinal]( const PropertyInfo& prop, void* pContainer )
        {
            if ( IMapContainerWrapper* pMap = prop._containerWrapper->asMap() )
                (void)pMap->eraseAt( pContainer, ordinal );
        } );
    }

    void PropertyGridModel::addMapEntry( uint32 rowID, string_view keyText )
    {
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr || pBinding->_pProp->_containerWrapper == nullptr || pBinding->_pProp->_containerWrapper->asMap() == nullptr )
            return;
        const PropertyInfo&     prop       = *pBinding->_pProp;
        IMapContainerWrapper&   mapWrapper = *prop._containerWrapper->asMap();
        void* const             pContainer = prop.getRawPtr( pBinding->_instance.get() );
        const SerializeContext& context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        if ( pContainer == nullptr )
            return;
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
                notifyRowEdited( rowID );
            }
        }
        mapWrapper.destroyKey( pKey );
        mapWrapper.destroyValue( pValue );
    }

    void PropertyGridModel::replaceKeyedElement( uint32 rowID, size_t index, string_view text )
    {
        applyContainerEdit( rowID, "Edit Set Element", [index, text]( const PropertyInfo& prop, void* pContainer )
        {
            ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
            if ( pSeq == nullptr )
                return;
            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
            (void)pSeq->replaceElement( pContainer, index, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
            { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, text, context ); } ) );
        } );
    }

    void PropertyGridModel::removeKeyedElement( uint32 rowID, size_t index )
    {
        applyContainerEdit( rowID, "Remove Set Element", [index]( const PropertyInfo& prop, void* pContainer )
        {
            if ( ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence() )
                (void)pSeq->eraseAt( pContainer, index );
        } );
    }

    void PropertyGridModel::addKeyedElement( uint32 rowID, string_view text )
    {
        applyContainerEdit( rowID, "Add Set Element", [text]( const PropertyInfo& prop, void* pContainer )
        {
            ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
            if ( pSeq == nullptr )
                return;
            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
            (void)pSeq->appendElement( pContainer, pSeq->getSize( pContainer ), SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
            { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, text, context ); } ) );
        } );
    }

    void PropertyGridModel::invokeMethod( const FunctionInfo& method, const TaskArgs& args )
    {
        if ( _pInstance == nullptr || _pType == nullptr )
            return;
        void* const     pInstance = _pInstance;
        const TypeInfo& type      = *_pType;
        TaskValue       result{};
        const auto      call = [&]()
        { result = getTypeRegistry().invokeMethod( pInstance, type._fullyQualifiedName, method._hashName, args ); };
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "Call %#", method._name.c_str() );
        PropertyGridModelInternal::applyEdit( _pObject, textBuilder.view(), call ); // 오브젝트 밖 대상(환경설정 구조체 등)은 되돌리기 대상이 없다
        InspectorBuiltinValue::formatMethodResult( result, method._returnTypeName, _lastInvokeResult.data(), _lastInvokeResult.capacity() );
    }

    bool PropertyGridModel::isEventBound( const EventInfo& event ) const
    {
        return _pInstance != nullptr && ReflectionInvoke::isEventBound( event, _pInstance );
    }

    void PropertyGridModel::broadcastEvent( const EventInfo& event )
    {
        if ( _pInstance != nullptr )
            (void)ReflectionInvoke::broadcastEvent( event, _pInstance, {} );
    }

    // ------------------------------------------------------------------------------
    // 값 모음
    // ------------------------------------------------------------------------------
    void PropertyGridModel::collectAssetEntries( uint32 rowID, vector<EditorAssetIndexEntry>& outListEntry ) const
    {
        outListEntry.clear();
        const RowBinding* pBinding = findValueBinding( rowID );
        if ( pBinding == nullptr )
            return;
        EditorAssetIndex::collectEntries( EditorPaths::getAssetPaths(), outListEntry );
        // 타입 있는 참조는 대상 종류가 거른다(언리얼 `TSoftObjectPtr<T>` 의 선택기와 같다). 그 밖의 경로 칸은 `FileFilter` 를 본다.
        const PropertyInfo& prop = *pBinding->_pProp;
        const AssetRefBase* pRef = prop._typeName == hashed_string( AssetRefBase::kTypeName ) ? prop.getValuePtr<AssetRefBase>( pBinding->_instance.get() ) : nullptr;
        const string_view   fileFilter{ prop._metadata->getEditor()._fileFilter };
        VectorUtil::removeIf( outListEntry, [pRef, fileFilter]( const EditorAssetIndexEntry& entry )
        { return ( pRef != nullptr ? AssetRefBase::isPathOfTarget( entry._path, pRef->getTargetType() ) : InspectorPropertyLayout::matchesFileFilter( fileFilter, entry._path ) ) == false; } );
    }

    bool PropertyGridModel::describeComponentHandle( const hashed_string& targetTypeName, const ComponentHandle& handle, string& outPreview, InspectorHandleState& outState ) const
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

    void PropertyGridModel::collectComponentChoices( const hashed_string& targetTypeName, vector<InspectorComponentChoice>& outListChoice ) const
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
    GameObject* PropertyGridModel::findSceneObject( EditorPropertyGridScope scope, uint64 objectID )
    {
        return PropertyGridModelInternal::findObject( scope, objectID );
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
