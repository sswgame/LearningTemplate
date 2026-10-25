#include "pch.h"

#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorObjectEdit.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/PropertyEditCondition.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPropertyGridInternal
        {
            static constexpr Color kNestedHeader{ 0.15f, 0.15f, 0.15f, 1.0f };
            static constexpr Color kCallInEditorButton{ 0.18f, 0.42f, 0.65f, 1.0f };
            static constexpr Color kCallInEditorButtonHovered{ 0.25f, 0.52f, 0.78f, 1.0f };
            static constexpr Color kCallInEditorButtonActive{ 0.12f, 0.35f, 0.55f, 1.0f };
            static constexpr Color kCallInEditorText{ 0.3f, 0.8f, 1.0f, 1.0f };

            /** @brief 컨테이너 구조 편집(추가, 삭제, 다시 넣기)입니다. 주인 오브젝트가 있으면 되돌리기에 남기고, 없으면 그냥 바꿉니다. */
            template <typename EditFunc>
            static void applyContainerEdit( GameObject* pOwner, string_view undoLabel, EditFunc&& edit )
            {
                if ( pOwner == nullptr )
                    edit();
                else
                    InspectorObjectEdit::applyObjectEdit( pOwner, undoLabel, std::forward<EditFunc>( edit ) );
            }

            /** @brief 즐겨찾기한 프로퍼티만 타입 표시 이름 머리 하나로 모아 @p inoutListGroup 을 바꿉니다. 하나도 없으면 비웁니다. */
            static void keepFavoriteProperties( const TypeInfo& typeInfo, const InspectorFavorites& favorites, vector<InspectorPropertyGroup>& inoutListGroup )
            {
                InspectorPropertyGroup favoriteGroup;
                favoriteGroup._category = typeInfo.getDisplayName();
                for ( const InspectorPropertyGroup& group : inoutListGroup )
                {
                    for ( const PropertyInfo* pProp : group._listProperty )
                    {
                        if ( favorites.contains( InspectorFavorites::makePropertyKey( typeInfo, *pProp ) ) )
                            favoriteGroup._listProperty.push_back( pProp );
                    }
                }
                inoutListGroup.clear();
                if ( favoriteGroup._listProperty.empty() == false )
                    inoutListGroup.push_back( std::move( favoriteGroup ) );
            }

            /**
             * @brief 기본값과 같은 프로퍼티를 빼고, 남은 것이 없는 카테고리는 머리째 뺍니다.
             * @details 다중 선택에서 값이 갈린(혼합) 프로퍼티는 남깁니다 — 값이 다르면 선택 중 적어도 하나는 기본값이 아닙니다. 갈리지 않았으면 모두 주 선택과 같으므로 주 선택만 보면 됩니다.
             */
            static void removeDefaultValued( MutableObjectAddress instance, ObjectAddress defaultInstance, Delegate<bool( const PropertyInfo& )>& isMixed, vector<InspectorPropertyGroup>& inoutListGroup )
            {
                for ( InspectorPropertyGroup& group : inoutListGroup )
                {
                    VectorUtil::removeIf( group._listProperty, [instance, defaultInstance, &isMixed]( const PropertyInfo* pProp )
                    {
                        const bool bMixed = isMixed.isBound() && isMixed( *pProp );
                        return bMixed == false && EditorDefaultObjects::isDefaultValue( *pProp, instance, defaultInstance );
                    } );
                }
                VectorUtil::removeIf( inoutListGroup, []( const InspectorPropertyGroup& group )
                { return group._listProperty.empty(); } );
            }

            /** @brief 비트 플래그 열거형을 콤보로 그립니다. 전체 선택 · 해제 · 항목 체크는 @p inoutEditedValue 만 바꿉니다. */
            static void drawBitFlagEnum( const EnumInfo& enumInfo, int64 currentValue, bool bReadOnly, int64& inoutEditedValue )
            {
                const utf8* pLabel = "##value";
                string      previewStr;
                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( val != 0 && ( currentValue & val ) == val )
                    {
                        if ( previewStr.empty() == false )
                            previewStr += " | ";
                        previewStr += nameHashed.c_str();
                    }
                }
                if ( previewStr.empty() )
                    previewStr = ( currentValue == 0 ) ? "None" : "<Unknown>";

                if ( bReadOnly )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( previewStr.c_str() );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, previewStr.c_str() ) == false )
                    return;
                if ( ImGui::SmallButton( "Select All" ) )
                {
                    for ( const auto& [val, _] : enumInfo._mapValueToName )
                    {
                        inoutEditedValue |= val;
                    }
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Clear All" ) )
                    inoutEditedValue = 0;
                ImGui::Separator();

                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    if ( val == 0 )
                        continue;
                    bool bChecked = ( ( inoutEditedValue & val ) == val );
                    if ( ImGui::Checkbox( nameHashed.c_str(), &bChecked ) )
                    {
                        if ( bChecked )
                            inoutEditedValue |= val;
                        else
                            inoutEditedValue &= ~val;
                    }
                }
                ImGui::EndCombo();
            }

            /** @brief 하나만 고르는 열거형을 콤보로 그립니다. 고른 값은 @p outEditedValue 에 둡니다. */
            static void drawPlainEnum( const TypeRegistry& registry, const EnumInfo& enumInfo, const PropertyInfo& prop, int64 currentValue, bool bReadOnly, int64& outEditedValue )
            {
                const utf8* pLabel   = "##value";
                const utf8* pName    = registry.enumToString( prop._typeName, currentValue );
                const utf8* pPreview = ( pName != nullptr ) ? pName : "<Unknown>";
                if ( bReadOnly )
                {
                    ImGui::TextDisabled( "%s", pLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( pPreview );
                    return;
                }

                if ( ImGui::BeginCombo( pLabel, pPreview ) == false )
                    return;
                for ( const auto& [val, nameHashed] : enumInfo._mapValueToName )
                {
                    const bool bSelected = ( val == currentValue );
                    if ( ImGui::Selectable( nameHashed.c_str(), bSelected ) )
                        outEditedValue = val;
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        // 시나리오가 콤보 항목을 누를 수 있게 한다(`inspector.enum.<프로퍼티>.<열거자>`) — 콤보를 연 프레임에만 그려진다.
                        fixed_string<constant::kMaxBuffer128> itemMark;
                        formatstring( itemMark.data(), itemMark.capacity(), "inspector.enum.%#.%#", prop._name.c_str(), nameHashed.c_str() );
                        EditorSelfTestMarks::note( itemMark.c_str() );
                    }
                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorPropertyGrid::EditorPropertyGrid()
        : _propertyFilter{}
        , _mapMethodArgSlot{}
        , _lastInvokeResult{}
        , _mapContainerAddText{}
        , _onEdited{}
        , _isMixed{}
        , _pEditTargetComponent{ nullptr }
        , _pEditTargetObject{ nullptr }
        , _propertyDrawDepth{ 0 }
        , _pFavorites{ nullptr }
        , _openAllRequest{ 0 }
        , _bFavoritesOnly{ false }
        , _bShowModifiedOnly{ false }
    {
    }

    void EditorPropertyGrid::drawSearchBarWithOptions( const utf8* pFieldTooltip )
    {
        const float32 buttonWidth = ImGui::GetFrameHeight();
        const float32 spacing     = ImGui::GetStyle().ItemSpacing.x;
        // drawSearchField 는 width 뒤에 지우기 단추(22 * dpi)를 더 그린다 — 둘을 빼야 보기 옵션 단추가 패널 오른쪽 밖으로 밀려나지 않는다.
        const float32 clearWidth = 22.0f * EditorThemeUtil::getDpiScale();
        const float32 fieldWidth = MathUtil::max( ImGui::GetContentRegionAvail().x - buttonWidth - clearWidth - spacing * 2.0f, 1.0f );
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), fieldWidth );
        if ( pFieldTooltip != nullptr )
            EditorWidgets::drawTooltip( pFieldTooltip );
        ImGui::SameLine();
        // 켜져 있으면 강조색으로 그려 거른 상태임을 알린다. 꺼진 줄 모르고 프로퍼티가 사라졌다고 여기지 않게 한다.
        if ( EditorWidgets::drawToggleIconButton( "##inspectorViewOptions", _bShowModifiedOnly, editoricon::kFilter, editoricon::kFilter,
                                                  SW_LOCKEY( "EditorInspector", "ViewOptions" ), SW_LOCKEY( "EditorInspector", "ViewOptions" ) ) )
            ImGui::OpenPopup( "InspectorViewOptions" );
        EditorSelfTestMarks::note( "inspector.viewOptions" );
        if ( ImGui::BeginPopup( "InspectorViewOptions" ) )
        {
            ImGui::MenuItem( "Show Only Modified Properties", nullptr, &_bShowModifiedOnly );
            EditorSelfTestMarks::note( "inspector.viewOptions.modifiedOnly" );
            ImGui::Separator();
            if ( ImGui::MenuItem( "Expand All" ) )
                _openAllRequest = 1;
            EditorSelfTestMarks::note( "inspector.viewOptions.expandAll" );
            if ( ImGui::MenuItem( "Collapse All" ) )
                _openAllRequest = -1;
            EditorSelfTestMarks::note( "inspector.viewOptions.collapseAll" );
            ImGui::EndPopup();
        }
    }

    void EditorPropertyGrid::drawSearchBar( const utf8* pMarkKey )
    {
        if ( pMarkKey != nullptr )
        {
            EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), 0.0f, false );
            EditorSelfTestMarks::note( pMarkKey );
            return;
        }
        EditorWidgets::drawSearchField( "##propFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ) );
    }

    void EditorPropertyGrid::drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        beginTarget( target );
        drawTypeProperties( target._pInstance, target._pType, pSectionTitle, listDrawnName );
        endTarget();
        // 편집 알림(다중 선택의 나머지에 값 적용)이 끝난 뒤에 되돌리기를 남긴다.
        InspectorPropertyUndo::commitFinishedEdits();
    }

    void EditorPropertyGrid::drawFavoriteProperties( const EditorPropertyGridTarget& target )
    {
        const bool bNoFavoritesToDraw = _pFavorites == nullptr || target._pType == nullptr || _pFavorites->hasAnyForTypeOrBase( *target._pType ) == false;
        if ( bNoFavoritesToDraw )
            return;
        // 본문 그리드와 같은 카테고리 · 프로퍼티 id 를 쓰므로 id 공간을 나눈다.
        ImGui::PushID( "##favorites" );
        _bFavoritesOnly = true;
        drawProperties( target, nullptr, {} );
        _bFavoritesOnly = false;
        ImGui::PopID();
    }

    void EditorPropertyGrid::drawMethodsAndEvents( const EditorPropertyGridTarget& target, bool bEvents )
    {
        beginTarget( target );
        drawTypeMethods( target._pInstance, target._pType );
        if ( bEvents )
            drawTypeEvents( target._pInstance, target._pType );
        endTarget();
    }

    void EditorPropertyGrid::resetPropertyToDefault( const MutableObjectAddress instance, const PropertyInfo& prop, const ObjectAddress defaultInstance )
    {
        const string text = defaultInstance.get() != nullptr ? SerializerUtil::formatPropertyText( prop, defaultInstance, SerializeContext::getDefault( SerializePurpose::Authoring ) )
                                                             : prop._metadata->_defaultValue;
        applyPropertyTextAsEdit( instance, prop, text, "Reset to Default" );
    }

    void EditorPropertyGrid::applyPropertyTextAsEdit( const MutableObjectAddress instance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel )
    {
        // 그 프로퍼티 하나만 쓰고(비트필드는 그 비트만) 알린 뒤 되돌리기에 남긴다. `{"이름":값}` 을 JSON 으로 읽히면 읽기에서 빠진
        // 프로퍼티마다 기본값을 채우므로 다른 프로퍼티까지 바뀐다. 그래서 텍스트 하나로 프로퍼티 하나만 쓴다.
        GameObject*  pOwnerObj = _pEditTargetComponent != nullptr ? _pEditTargetComponent->getOwner() : _pEditTargetObject;
        const string value{ text };
        auto         apply = [this, &prop, instance, &value]()
        {
            if ( SerializerUtil::applyPropertyText( prop, instance, value, SerializeContext::getDefault( SerializePurpose::Authoring ) ) )
                notifyPropertyEdited( prop );
            else
                SW_LOG_WARNING( "'%#' could not be applied to %#", value.c_str(), prop._name.c_str() );
        };
        if ( pOwnerObj != nullptr )
            InspectorObjectEdit::applyObjectEdit( pOwnerObj, pUndoLabel, apply );
        else
            apply();
    }

    void EditorPropertyGrid::beginTarget( const EditorPropertyGridTarget& target )
    {
        _pEditTargetComponent = target._pComponent;
        _pEditTargetObject    = target._pComponent == nullptr ? target._pObject : nullptr;
        _onEdited             = target._onEdited;
        _isMixed              = target._isMixed;
    }

    void EditorPropertyGrid::endTarget()
    {
        _pEditTargetComponent = nullptr;
        _pEditTargetObject    = nullptr;
        _onEdited             = {};
        _isMixed              = {};
    }

    void EditorPropertyGrid::drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName )
    {
        using Internal = EditorPropertyGridInternal;

        // 즐겨찾기 거르기는 이번 한 번만이다. 중첩 그리기가 다시 들어와도 거르지 않게 먼저 지운다.
        const bool bFavoritesOnly = _bFavoritesOnly && _pFavorites != nullptr;
        _bFavoritesOnly           = false;
        if ( pInstance == nullptr || pTypeInfo == nullptr )
            return;

        // 무엇을 어떤 순서로 그릴지는 `InspectorPropertyLayout` 이 정한다. 상속한 프로퍼티까지 포함하고, 카테고리는 기반 클래스부터 처음 나온 순서다.
        const EditorListFilter         filter{ _propertyFilter.c_str() };
        vector<InspectorPropertyGroup> listGroup;
        InspectorPropertyLayout::collectPropertyGroups( *pTypeInfo, listDrawnName, filter, listGroup );

        // 즐겨찾기 구역이면 즐겨찾기한 프로퍼티만 타입 이름 머리 하나로 모은다.
        if ( bFavoritesOnly )
        {
            Internal::keepFavoriteProperties( *pTypeInfo, *_pFavorites, listGroup );
            if ( listGroup.empty() )
                return;
        }

        // 검색어가 아무 프로퍼티도 맞히지 못하면 그렇다고 알려 준다.
        if ( listGroup.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            return;
        }
        // 행 메뉴의 즐겨찾기 토글은 대상 자신(컴포넌트나 오브젝트)의 프로퍼티에만 둔다. 중첩 구조체의 필드는 키가 대상 타입이 아니다.
        const bool bFavoriteTarget = _pFavorites != nullptr &&
                                     ( pInstance == static_cast<void*>( _pEditTargetComponent ) || pInstance == static_cast<void*>( _pEditTargetObject ) );

        if ( pSectionTitle != nullptr )
            ImGui::SeparatorText( pSectionTitle );
        // 컴포넌트이면 그 타입의 기본 인스턴스(CDO)와 비교한다. 중첩 구조체와 씬 밖 객체는 메타의 기본값 텍스트만 있다.
        const MutableObjectAddress instance{ pInstance };
        const void*                pDefaultInstance   = nullptr;
        EditorContext*             pDefaultContext    = EditorContext::get();
        const bool                 bIsEditedComponent = _pEditTargetComponent != nullptr && pInstance == _pEditTargetComponent && pDefaultContext != nullptr;
        if ( bIsEditedComponent )
            pDefaultInstance = pDefaultContext->getSubsystem<EditorDefaultObjects>().findDefault( *pTypeInfo );
        const ObjectAddress defaultInstance{ pDefaultInstance };
        // 바뀐 것만 보기이면 기본값과 같은 프로퍼티를 뺀다. 남은 것이 없는 카테고리는 머리째 감춘다.
        if ( _bShowModifiedOnly )
        {
            Internal::removeDefaultValued( instance, defaultInstance, _isMixed, listGroup );
            if ( listGroup.empty() )
                return;
        }
        for ( const InspectorPropertyGroup& group : listGroup )
        {
            const string& category = group._category;
            const auto&   props    = group._listProperty;
            if ( _openAllRequest != 0 )
                ImGui::SetNextItemOpen( _openAllRequest > 0 );
            if ( ImGui::CollapsingHeader( category.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;

            if ( ImGui::BeginTable( category.c_str(), 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg ) )
            {
                ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthFixed, 150.0f * EditorThemeUtil::getDpiScale() );
                ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch );

                for ( const PropertyInfo* prop : props )
                {
                    // `EditCondition` 이 거짓이면 숨기거나(EditConditionHides) 막는다. 판정은 ImGui 에 의존하지 않는 `PropertyEditCondition` 이 한다.
                    const PropertyEditState editState = PropertyEditCondition::getEditState( *pTypeInfo, *prop, instance );
                    if ( editState == PropertyEditState::Hidden )
                        continue;

                    drawPropertyRow( instance, *pTypeInfo, *prop, editState == PropertyEditState::Disabled, bFavoriteTarget, defaultInstance );
                }
                ImGui::EndTable();
            }
        }
    }

    void EditorPropertyGrid::drawPropertyRow( const MutableObjectAddress instance, const TypeInfo& typeInfo, const PropertyInfo& prop, bool bDisabled, bool bFavoriteTarget, const ObjectAddress defaultInstance )
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        if ( _isMixed.isBound() && _isMixed( prop ) )
        {
            ImGui::TextDisabled( "\xe2\x80\x94" ); // — (혼합)
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "MultipleValues" ) );
            ImGui::SameLine();
        }
        const string favoriteKey = bFavoriteTarget ? InspectorFavorites::makePropertyKey( typeInfo, prop ) : string{};
        const bool   bFavorite   = bFavoriteTarget && _pFavorites->contains( favoriteKey );
        if ( bFavorite )
        {
            ImGui::TextUnformatted( editoricon::kStar );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "FavoriteProperty" ) );
            ImGui::SameLine();
        }
        ImGui::TextUnformatted( InspectorPropertyLayout::getPropertyLabel( prop ) );
        EditorWidgets::drawTooltip( prop._metadata->_tooltip.c_str() );
        // 시나리오가 행 메뉴를 오른쪽 클릭으로 열 수 있게 이름 글에 레이블을 남긴다(`inspector.label.<타입>.<프로퍼티>`).
        fixed_string<constant::kMaxBuffer128> labelMark;
        formatstring( labelMark.data(), labelMark.capacity(), "inspector.label.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
        EditorSelfTestMarks::note( labelMark.c_str() );

        drawPropertyRowMenu( instance, prop, favoriteKey, bFavoriteTarget, bFavorite, defaultInstance );

        // 기본값과 다르면 이름 영역 끝에 되돌리기 버튼(언리얼의 노란 화살표)을 둔다. 기본값은 컴포넌트의 기본 인스턴스(CDO)이고, 없으면 메타 텍스트다.
        if ( EditorDefaultObjects::isDefaultValue( prop, instance, defaultInstance ) == false )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( editoricon::kRefresh ) )
                resetPropertyToDefault( instance, prop, defaultInstance );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ResetToDefaultTooltip" ) );
            fixed_string<constant::kMaxBuffer128> resetMark;
            formatstring( resetMark.data(), resetMark.capacity(), "inspector.reset.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
            EditorSelfTestMarks::note( resetMark.c_str() );
        }

        if ( prop._metadata->_bTransient == SW_TRUE )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(T)" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "TransientProperty" ) );
        }

        ImGui::TableNextColumn();
        ImGui::PushID( prop._name.c_str() );
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::BeginDisabled( bDisabled );
        drawPropertyWidget( instance, prop );
        ImGui::EndDisabled();
        // 시나리오가 누를 수 있게 값 입력란에 이름을 남긴다(`inspector.property.<타입>.<프로퍼티>`). 테스트를 켰을 때만 기록한다.
        fixed_string<constant::kMaxBuffer128> mark;
        formatstring( mark.data(), mark.capacity(), "inspector.property.%#.%#", typeInfo._name.c_str(), prop._name.c_str() );
        EditorSelfTestMarks::note( mark.c_str() );
        ImGui::PopID();
    }

    void EditorPropertyGrid::drawPropertyRowMenu( const MutableObjectAddress instance, const PropertyInfo& prop, const string& favoriteKey, bool bFavoriteTarget, bool bFavorite, const ObjectAddress defaultInstance )
    {
        if ( ImGui::BeginPopupContextItem( "PropCtx" ) == false )
            return;

        // 언리얼 Details 의 행 메뉴 Add to Favorites · Remove from Favorites 에 해당한다. 바꾸면 바로 저장한다.
        if ( bFavoriteTarget )
        {
            if ( ImGui::MenuItem( bFavorite ? SW_LOCKEY( "EditorInspector", "RemoveFromFavorites" ) : SW_LOCKEY( "EditorInspector", "AddToFavorites" ) ) )
            {
                (void)_pFavorites->toggle( favoriteKey );
                (void)_pFavorites->saveFile( InspectorFavorites::getDefaultFilePath() ); // 실패는 saveFile 이 경고한다
            }
            ImGui::Separator();
        }
        if ( defaultInstance.get() != nullptr || prop._metadata->_defaultValue.empty() == false )
        {
            if ( ImGui::MenuItem( "Reset to Default" ) )
                resetPropertyToDefault( instance, prop, defaultInstance );
        }
        if ( ImGui::MenuItem( "Copy Value" ) )
            ImGui::SetClipboardText( SerializerUtil::formatPropertyText( prop, instance, SerializeContext::getDefault( SerializePurpose::Authoring ) ).c_str() );
        // 타입 핸들은 선택기만이 입력 길이다. 글로 붙이면 대상 타입을 확인하지 않은 값이 들어간다.
        const bool bPasteAllowed = ImGui::GetClipboardText() != nullptr && prop._handleTargetTypeName.empty();
        if ( ImGui::MenuItem( "Paste Value", nullptr, false, bPasteAllowed ) )
            applyPropertyTextAsEdit( instance, prop, ImGui::GetClipboardText(), "Paste Value" );
        EditorSelfTestMarks::note( "inspector.menu.pasteValue" );
        if ( ImGui::MenuItem( "Copy Property Name" ) )
            ImGui::SetClipboardText( prop._name.c_str() );
        ImGui::EndPopup();
    }

    void EditorPropertyGrid::drawPropertyWidget( const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        // ImGui 는 이번 프레임에 활성 아이템이 편집됐는지를 컨텍스트에 전역으로 기록한다. 위젯을
        // 그리기 전후로 그 플래그의 **변화**를 보면 "그 사이에 그린 이 프로퍼티가 편집됐다" 를 정확히
        // 가려낼 수 있다. 위젯이 아이템 하나든 여럿이든, 값이 float 이든 string 이든 상관없다.
        // 위젯마다 판정을 넣으면 새 위젯을 추가할 때 빠뜨리기 쉽지만, 여기 한 곳에 두면 빠뜨릴 수 없다.
        ImGuiContext& g             = *ImGui::GetCurrentContext();
        const bool    bEditedBefore = g.ActiveIdHasBeenEditedThisFrame;

        ++_propertyDrawDepth;
        drawPropertyWidgetBody( instance, prop );
        --_propertyDrawDepth;

        // 컨테이너와 중첩 구조체는 이 함수가 재귀한다. 알림은 가장 바깥에서 한 번만 한다.
        const bool bOutermostEdit = _propertyDrawDepth == 0 && bEditedBefore == false && g.ActiveIdHasBeenEditedThisFrame;
        if ( bOutermostEdit )
            notifyPropertyEdited( prop );
    }

    void EditorPropertyGrid::notifyPropertyEdited( const PropertyInfo& prop )
    {
        if ( _pEditTargetComponent != nullptr )
            _pEditTargetComponent->onPropertyChanged( prop._name );
        else if ( _pEditTargetObject != nullptr )
            _pEditTargetObject->onPropertyChanged( prop._name );

        // 씬 밖 객체(환경설정, 문서)는 대상이 지정한 곳으로 알린다.
        if ( _onEdited.isBound() )
            _onEdited( prop );

        // 고친 값을 검증해 맵 검사 결과를 바꾼다. 로그는 남기지 않는다. 끄는 동안 프레임마다 호출되기 때문이다.
        const GameObject* pOwner = ( _pEditTargetComponent != nullptr ) ? _pEditTargetComponent->getOwner() : _pEditTargetObject;
        if ( pOwner != nullptr )
            (void)ObjectValidation::reportGameObject( *pOwner, false );
    }

    void EditorPropertyGrid::drawPropertyWidgetBody( const MutableObjectAddress instance, const PropertyInfo& prop )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        IInspectorProperty* pProperty = pContext->getSubsystem<InspectorPropertyManager>().find( prop._typeName.c_str() );
        if ( pProperty != nullptr )
        {
            if ( pProperty->draw( instance, prop, *this ) )
                return;
        }

        const bool bReadOnly = prop._metadata->_bReadOnly != SW_FALSE;

        auto* pRegistry = editor::findSubsystem<TypeRegistry>();
        if ( pRegistry == nullptr )
            return;

        const EnumInfo* pEnumInfo = pRegistry->findEnum( prop._typeName );
        if ( pEnumInfo != nullptr )
        {
            drawEnumProperty( instance, prop, *pEnumInfo, bReadOnly );
            return;
        }

        if ( prop._bIsContainer && prop._containerWrapper != nullptr )
        {
            drawContainerProperty( instance, prop, bReadOnly );
            return;
        }

        const TypeInfo* pFieldType = pRegistry->findType( prop._typeName );
        if ( pFieldType != nullptr || prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
        {
            drawStructOrStringProperty( instance, prop, pFieldType );
            return;
        }

        ImGui::TextDisabled( "No inspector for %s", prop._typeName.c_str() );
    }

    void EditorPropertyGrid::drawEnumProperty( const MutableObjectAddress instance, const PropertyInfo& prop, const EnumInfo& enumInfo, bool bReadOnly )
    {
        using Internal = EditorPropertyGridInternal;

        auto* pRegistry = editor::findSubsystem<TypeRegistry>();
        if ( pRegistry == nullptr )
            return;

        // enum 의 실제 크기(`EnumInfo::_size` 와 부호)로 읽고 쓴다. 항상 int32 로 읽고 쓰면, 엔진의 enum 은 대부분 uint8 이나
        // uint16 이므로 하나를 고를 때마다 뒤의 필드를 덮어쓰고(`CameraComponent::_role` 을 고르면 바로 뒤의 `_bOrthographic` 이 꺼진다),
        // 읽을 때는 이웃 바이트가 섞여 멀쩡한 값이 "<Unknown>" 으로 보인다.
        void* pEnumMemory = prop.getValuePtr<void>( instance.get() );
        if ( pEnumMemory == nullptr )
            return;
        const int64 currentValue = enumInfo.readValueFromMemory( pEnumMemory );
        int64       editedValue  = currentValue;

        if ( enumInfo._bIsBitFlag )
            Internal::drawBitFlagEnum( enumInfo, currentValue, bReadOnly, editedValue );
        else
            Internal::drawPlainEnum( *pRegistry, enumInfo, prop, currentValue, bReadOnly, editedValue );
        if ( bReadOnly )
            return;

        // 콤보에서 고른 값은 고르는 순간 바뀐다 — 콤보 단추의 활성화 · 해제로는 잡히지 않으므로 쓰기 직전에 편집 전을 찍어 남긴다.
        if ( editedValue != currentValue )
        {
            InspectorPropertyUndo::recordImmediateEdit( prop );
            enumInfo.writeValueToMemory( pEnumMemory, editedValue );
        }
    }

    void EditorPropertyGrid::drawContainerProperty( const MutableObjectAddress instance, const PropertyInfo& prop, bool bReadOnly )
    {
        const utf8* pLabel = "##value";

        // 이 함수의 모든 분기는 무언가를 그리고 끝난다. 아무것도 그리지 않고 반환하면 인스펙터에는 빈 항목 하나만 남아,
        // 값이 비었는지, 그리지 못하는 것인지, 버그인지 화면만 보고는 구분할 수 없다. 모르는 타입도 "No inspector for ..." 라고
        // 알린다. 맵 프로퍼티도 실제로 있다.
        void* pContainer = prop.getRawPtr( instance.get() );
        if ( pContainer == nullptr )
        {
            ImGui::TextDisabled( "컨테이너를 읽을 수 없습니다 (%s)", prop._typeName.c_str() );
            return;
        }

        ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
        if ( pSeq == nullptr )
        {
            IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
            if ( pMap == nullptr )
            {
                ImGui::TextDisabled( "No inspector for %s", prop._typeName.c_str() );
                return;
            }

            drawMapContainer( MutableValueAddress{ pContainer }, prop, *pMap, bReadOnly );
            return;
        }
        // 원소가 곧 정렬 키이거나 해시 키인 컨테이너(`set`)는 제자리에서 고칠 수 없으므로 지우고 다시 넣는다.
        if ( pSeq->allowsInPlaceElementWrite() == false )
        {
            drawKeyedSequenceContainer( MutableValueAddress{ pContainer }, prop, *pSeq, bReadOnly );
            return;
        }

        const size_t                          count = pSeq->getSize( pContainer );
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );

        const bool bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        if ( ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) )
        {
            // 고정 배열은 원소 수가 정해져 있다. 추가와 비우기는 아무 일도 하지 않으므로 그리지 않는다.
            if ( bReadOnly == false && pSeq->isFixedSize() == false )
            {
                if ( ImGui::SmallButton( "+ Add" ) )
                {
                    EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Add Element", [pSeq, pContainer]()
                    { pSeq->addElementDefault( pContainer ); } );
                    notifyPropertyEdited( prop );
                }
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Clear" ) )
                {
                    EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Clear Elements", [pSeq, pContainer]()
                    { pSeq->clear( pContainer ); } );
                    notifyPropertyEdited( prop );
                }
                ImGui::Separator();
            }

            // 원소 위젯도 같은 규칙을 받아야 한다. `bReadOnly` 가 위의 버튼만 가리면
            // `ReadOnly` 컨테이너의 원소가 그대로 편집된다.
            ImGui::BeginDisabled( bElementEditable == false );

            const size_t newCount = pSeq->getSize( pContainer );
            for ( size_t elemIndex = 0; elemIndex < newCount; ++elemIndex )
            {
                void* pElem = pSeq->getElement( pContainer, elemIndex );
                if ( pElem == nullptr )
                    continue;

                ImGui::PushID( static_cast<int32>( elemIndex ) );
                ImGui::AlignTextToFramePadding();
                ImGui::Text( "[%zu]", elemIndex );
                ImGui::SameLine();

                const PropertyInfo elemProp = InspectorPropertyLayout::makeElementProperty( prop );

                // 원소 프로퍼티는 오프셋 0 이라 원소의 값 주소가 곧 그 프로퍼티를 가진 객체 주소다.
                ImGui::SetNextItemWidth( -FLT_MIN );
                drawPropertyWidget( MutableObjectAddress{ pElem }, elemProp );
                ImGui::PopID();
            }

            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    void EditorPropertyGrid::drawMapContainer( const MutableValueAddress container, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly )
    {
        void* const                           pContainer = container.get();
        const SerializeContext&               context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%# -> %#] (%# entries)", prop._keyTypeName.c_str(), prop._elementTypeName.c_str(),
                      mapWrapper.getSize( pContainer ) );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        const PropertyInfo valueProp        = InspectorPropertyLayout::makeElementProperty( prop );
        const bool         bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );

        // 값은 맵 노드 안에서 바로 고친다. 되돌리기는 위젯 하나 단위다(`drawPropertyWidget`). 삭제는 순회가 끝난 뒤에 한다.
        size_t eraseOrdinal = static_cast<size_t>( -1 );
        size_t ordinal      = 0;
        mapWrapper.forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* pKey, void* pValue )
        {
            ImGui::PushID( static_cast<int32>( ordinal ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseOrdinal = ordinal;
            if ( bReadOnly == false )
                ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( SerializerUtil::formatValueText( ValueAddress{ pKey }, prop._keyTypeName, context ).c_str() );
            ImGui::SameLine();
            ImGui::BeginDisabled( bElementEditable == false );
            ImGui::SetNextItemWidth( -FLT_MIN );
            drawPropertyWidget( MutableObjectAddress{ pValue }, valueProp ); // 값 프로퍼티는 오프셋 0 이라 값 주소가 곧 객체 주소다
            ImGui::EndDisabled();
            ImGui::PopID();
            ++ordinal;
        } ) );

        if ( eraseOrdinal != static_cast<size_t>( -1 ) )
        {
            EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Remove Map Entry", [&mapWrapper, pContainer, eraseOrdinal]()
            { (void)mapWrapper.eraseAt( pContainer, eraseOrdinal ); } );
            notifyPropertyEdited( prop );
        }

        string keyText;
        if ( bReadOnly == false && drawContainerAddRow( SW_LOCKEY( "EditorInspector", "NewKeyHint" ), keyText ) )
        {
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
                    EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Add Map Entry", [&mapWrapper, pContainer, pKey, pValue]()
                    { mapWrapper.insertKeyValue( pContainer, pKey, pValue ); } );
                    notifyPropertyEdited( prop );
                }
            }
            mapWrapper.destroyKey( pKey );
            mapWrapper.destroyValue( pValue );
        }
        ImGui::TreePop();
    }

    void EditorPropertyGrid::drawKeyedSequenceContainer( const MutableValueAddress container, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly )
    {
        void* const                           pContainer = container.get();
        const SerializeContext&               context    = SerializeContext::getDefault( SerializePurpose::Authoring );
        const size_t                          count      = sequence.getSize( pContainer );
        fixed_string<constant::kMaxBuffer128> headerBuf;
        formatstring( headerBuf.data(), headerBuf.capacity(), "[%#] (%# elements)", prop._elementTypeName.c_str(), count );
        if ( ImGui::TreeNodeEx( "##value", ImGuiTreeNodeFlags_SpanFullWidth, "%s", headerBuf.c_str() ) == false )
            return;

        // 원소는 값 주소다. 텍스트 변환은 값 주소 쪽(`formatValueText`, `parseTextValue`)을 원소 타입 이름으로 쓴다.
        const hashed_string& elementTypeName  = prop._elementTypeName;
        const bool           bElementEditable = EditorSessionPolicy::areContainerElementEditsAllowed( bReadOnly, true );
        size_t               eraseIndex       = static_cast<size_t>( -1 );
        size_t               replaceIndex     = static_cast<size_t>( -1 );
        string               replaceText;
        for ( size_t elemIndex = 0; elemIndex < count; ++elemIndex )
        {
            ImGui::PushID( static_cast<int32>( elemIndex ) );
            if ( bReadOnly == false && ImGui::SmallButton( "x" ) )
                eraseIndex = elemIndex;
            if ( bReadOnly == false )
                ImGui::SameLine();
            // 원소는 텍스트 입력란이다. Enter 로 확정한 텍스트를 원소 타입으로 읽어 지우고 다시 넣는다. 원소가 곧 정렬 키이므로 제자리에 쓰면 트리가 깨진다.
            fixed_string<constant::kMaxBuffer256> elementText{ SerializerUtil::formatValueText( ValueAddress{ sequence.getElementConst( pContainer, elemIndex ) }, elementTypeName, context ).c_str() };
            const ImGuiInputTextFlags             flags = bElementEditable ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_ReadOnly;
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( ImGui::InputText( "##element", elementText.data(), elementText.capacity(), flags ) && bElementEditable )
            {
                replaceIndex = elemIndex;
                replaceText  = elementText.c_str();
            }
            ImGui::PopID();
        }

        if ( replaceIndex != static_cast<size_t>( -1 ) )
        {
            EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Edit Set Element", [&]()
            {
                (void)sequence.replaceElement( pContainer, replaceIndex, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, elementTypeName, replaceText, context ); } ) );
            } );
            notifyPropertyEdited( prop );
        }
        else if ( eraseIndex != static_cast<size_t>( -1 ) )
        {
            EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Remove Set Element", [&sequence, pContainer, eraseIndex]()
            { (void)sequence.eraseAt( pContainer, eraseIndex ); } );
            notifyPropertyEdited( prop );
        }

        string addText;
        if ( bReadOnly == false && drawContainerAddRow( SW_LOCKEY( "EditorInspector", "NewElementHint" ), addText ) )
        {
            EditorPropertyGridInternal::applyContainerEdit( getEditOwner(), "Add Set Element", [&]()
            {
                (void)sequence.appendElement( pContainer, sequence.getSize( pContainer ), SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, elementTypeName, addText, context ); } ) );
            } );
            notifyPropertyEdited( prop );
        }
        ImGui::TreePop();
    }

    bool EditorPropertyGrid::drawContainerAddRow( const utf8* pHint, string& outText )
    {
        const ImGuiID                          rowID = ImGui::GetID( "##add" );
        fixed_string<constant::kMaxBuffer256>& text  = _mapContainerAddText[static_cast<uint32>( rowID )];
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool bEntered = ImGui::InputTextWithHint( "##add", pHint, text.data(), text.capacity(), ImGuiInputTextFlags_EnterReturnsTrue );
        if ( bEntered == false || text.empty() )
            return false;
        outText = text.c_str();
        text.clear();
        return true;
    }

    GameObject* EditorPropertyGrid::getEditOwner() const
    {
        if ( _pEditTargetComponent != nullptr )
            return _pEditTargetComponent->getOwner();
        return _pEditTargetObject;
    }

    void EditorPropertyGrid::drawStructOrStringProperty( const MutableObjectAddress instance, const PropertyInfo& prop, const TypeInfo* pFieldType )
    {
        const utf8* pLabel = "##value";

        void* pNestedPtr = prop.getRawPtr( instance.get() );
        if ( pNestedPtr == nullptr )
            return;

        ImGui::PushStyleColor( ImGuiCol_Header, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kNestedHeader ) );
        bool bNodeOpen = ImGui::TreeNodeEx( pLabel, ImGuiTreeNodeFlags_SpanFullWidth, "[%s]", prop._typeName.c_str() );
        ImGui::PopStyleColor();

        if ( bNodeOpen )
        {
            pFieldType->forEachProperty( [&]( const PropertyInfo& nestedProp )
            {
                // 구조체 필드의 값 주소는 그 구조체 타입의 객체 주소다.
                const PropertyEditState editState = PropertyEditCondition::getEditState( *pFieldType, nestedProp, ObjectAddress{ pNestedPtr } );
                if ( editState == PropertyEditState::Hidden )
                    return;
                ImGui::PushID( nestedProp._name.c_str() );
                ImGui::AlignTextToFramePadding();
                ImGui::BulletText( "%s", InspectorPropertyLayout::getPropertyLabel( nestedProp ) );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( -FLT_MIN );
                ImGui::BeginDisabled( editState == PropertyEditState::Disabled );
                drawPropertyWidget( MutableObjectAddress{ pNestedPtr }, nestedProp );
                ImGui::EndDisabled();
                ImGui::PopID();
            }, true ); // 구조체의 기반 필드도 그린다(컴포넌트와 같은 규칙)
            ImGui::TreePop();
        }
        return;
    }

    void EditorPropertyGrid::drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo )
    {
        const bool bNoMethodsToDraw = pInstance == nullptr || pTypeInfo == nullptr || pTypeInfo->_listMethod.empty();
        if ( bNoMethodsToDraw )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInspector", "NoFunctionMethods" ) );
            return;
        }

        if ( _lastInvokeResult.empty() == false )
            ImGui::TextDisabled( "Last result: %s", _lastInvokeResult.c_str() );

        for ( const FunctionInfo& method : pTypeInfo->_listMethod )
        {
            // 생성자 호출기는 초기화되지 않은 저장 공간(placement new)을 기대한다. 살아 있는 인스펙터 인스턴스에 호출하면 안전하지 않다.
            if ( method._metadata->_bConstructor != SW_FALSE )
                continue;

            const utf8* pLabelName = method._name.c_str();
            if ( method._metadata->_displayName.empty() == false )
                pLabelName = method._metadata->_displayName.c_str();

            const uint32 paramCount = method.getParameterCount();

            if ( method._metadata->_bCallInEditor != SW_FALSE && paramCount == 0 )
            {
                ImGui::PushID( method._hashName.c_str() );
                ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kCallInEditorButton ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kCallInEditorButtonHovered ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorColorUtil::toImVec4( EditorPropertyGridInternal::kCallInEditorButtonActive ) );

                fixed_string<constant::kMaxBuffer128> buttonLabel;
                formatstring( buttonLabel.data(), buttonLabel.capacity(), "Run %#", pLabelName );
                if ( ImGui::Button( buttonLabel.c_str(), ImVec2{ -FLT_MIN, 0.0f } ) )
                {
                    invokeTypeMethod( pInstance, pTypeInfo, method, TaskArgs{} );
                }
                ImGui::PopStyleColor( 3 );
                EditorWidgets::drawTooltip( method._metadata->_tooltip.c_str() );
                ImGui::PopID();
                continue;
            }

            ImGui::PushID( method._hashName.c_str() );
            ImGui::Text( "%s (%s)", pLabelName,
                         method._returnTypeName.empty() ? "?" : method._returnTypeName.c_str() );
            if ( method._metadata->_bCallInEditor != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextColored( EditorColorUtil::toImVec4( EditorPropertyGridInternal::kCallInEditorText ), "[Editor]" );
            }
            EditorWidgets::drawTooltip( method._metadata->_tooltip.c_str() );
            // 키는 타입 · 메서드 이름 원문이다(해시 조합은 부딪히면 다른 메서드의 인자 슬롯을 쓴다).
            const string                    slotKey  = string( pTypeInfo->_fullyQualifiedName.c_str() ) + '\x1F' + method._hashName.c_str();
            vector<InspectorMethodArgSlot>& listSlot = _mapMethodArgSlot[slotKey];
            const bool                      bArgsOk  = InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot );
            for ( uint32 paramIndex = 0; paramIndex < static_cast<uint32>( listSlot.size() ); ++paramIndex )
            {
                ImGui::PushID( static_cast<int32>( paramIndex ) );
                // 선언에 인자 이름이 있으면 그것을 쓰고, 없으면 순번을 쓴다.
                const FunctionParameterInfo&         parameter = method._listParameter[paramIndex];
                fixed_string<constant::kMaxBuffer64> label;
                if ( parameter._name.empty() )
                    formatstring( label.data(), label.capacity(), "arg%# (%#)", paramIndex, parameter._typeName.c_str() );
                else
                    formatstring( label.data(), label.capacity(), "%# (%#)", parameter._name.c_str(), parameter._typeName.c_str() );
                const GameObject* pEditOwner = getEditOwner();
                InspectorPropertyManager::drawMethodArg( label.c_str(), listSlot[paramIndex], parameter, pEditOwner != nullptr ? pEditOwner->getManager() : nullptr );
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    // 시나리오가 인자 입력란을 누른다(핸들 선택기의 콤보가 마지막 항목이다). 인자 레이블 글("mesh (ComponentHandle)")이 오른쪽에 붙어
                    // 좁은 창에서 위젯 사각형의 가운데가 레이블 글 위에 놓이므로 입력 칸만 표식으로 남긴다.
                    const string argMark = string( "inspector.method.arg." ) + method._name.c_str() + "." + to_string( paramIndex );
                    EditorSelfTestMarks::noteInputFrame( argMark.c_str() );
                }
                ImGui::PopID();
            }

            if ( paramCount > InspectorBuiltinValueUtil::kMaxMethodArgCount )
                ImGui::TextDisabled( "Too many arguments (max %u in UI).", InspectorBuiltinValueUtil::kMaxMethodArgCount );

            if ( bArgsOk == false )
            {
                ImGui::BeginDisabled();
                ImGui::Button( "Invoke" );
                ImGui::EndDisabled();
                ImGui::TextDisabled( "Unsupported FUNCTION args - invoke skipped." );
            }
            else
            {
                const bool bInvoke = ImGui::Button( "Invoke" );
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    const string invokeMark = string( "inspector.method.invoke." ) + method._name.c_str();
                    EditorSelfTestMarks::note( invokeMark.c_str() );
                }
                if ( bInvoke )
                    invokeTypeMethod( pInstance, pTypeInfo, method, InspectorBuiltinValueUtil::makeMethodArgs( listSlot ) );
            }

            ImGui::PopID();
        }
    }

    void EditorPropertyGrid::drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo )
    {
        bool bHasEvent = false;
        pTypeInfo->forEachEventWithBase( [&bHasEvent]( const EventInfo& )
        { bHasEvent = true; } );
        if ( bHasEvent == false )
            return;

        ImGui::SeparatorText( "Events" );
        pTypeInfo->forEachEventWithBase( [pInstance]( const EventInfo& event )
        {
            ImGui::PushID( event._name.c_str() );
            const string signature = InspectorPropertyLayout::formatParameterList( event._listParameter );
            ImGui::BulletText( "%s(%s)", event._name.c_str(), signature.c_str() );
            EditorWidgets::drawTooltip( event._metadata->_tooltip.c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", ReflectionInvoke::isEventBound( event, pInstance ) ? "(bound)" : "(unbound)" );
            if ( event.getParameterCount() == 0 )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Broadcast" ) )
                    (void)ReflectionInvoke::broadcastEvent( event, pInstance, {} );
            }
            ImGui::PopID();
        } );
    }

    void EditorPropertyGrid::invokeTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method,
                                               const TaskArgs& args )
    {
        // `findSubsystem<T>()` 는 nullptr 을 반환할 수 있다. 역참조하기 전에 확인한다.
        TypeRegistry* pTypeRegistry = editor::findSubsystem<TypeRegistry>();
        if ( pTypeRegistry == nullptr )
            return;

        const TaskValue result = pTypeRegistry->invokeMethod( pInstance, pTypeInfo->_fullyQualifiedName, method._hashName, args );
        InspectorBuiltinValueUtil::formatMethodResult( result, method._returnTypeName, _lastInvokeResult.data(),
                                                       _lastInvokeResult.capacity() );
    }
} // namespace sw::editor
