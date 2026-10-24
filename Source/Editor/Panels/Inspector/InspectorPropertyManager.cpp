#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyManager.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/Asset/EditorResourceIndex.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/ContentBrowser/ContentBrowserPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/InspectorComponentPicker.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace sw::editor
{
    span<const utf8* const> InspectorPropertyManager::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorPanelManager::kSubsystemName };
        return kArrDependency;
    }

    namespace
    {
        /** @brief 애셋 경로 필드에서 이번 프레임에 일어난 일을 나타냅니다. */
        enum class AssetFieldAction : uint8
        {
            None,
            Dropped, ///< 드래그 앤 드롭이나 선택 팝업으로 경로가 들어왔다
            Cleared, ///< 지우기 버튼을 눌렀다
        };

        /** @brief 에셋 선택 팝업의 상태입니다. 열린 팝업은 하나뿐이므로 상태도 하나만 보관합니다. */
        struct AssetPickerState
        {
            vector<EditorResourceIndexEntry>     _listEntry; ///< 팝업을 열 때 한 번 모은 리소스다. 파일 목록을 프레임마다 스캔하지 않는다
            fixed_string<constant::kMaxBuffer64> _search;
        };

        /** @brief 열린 선택 팝업의 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
        AssetPickerState& getAssetPickerState()
        {
            return getEditorState<AssetPickerState>();
        }

        class BuiltinPropertyBase : public IInspectorProperty
        {
        protected:
            const utf8* _pLabel = "##value";
            void        showTooltipIfHovered( const PropertyInfo& prop )
            {
                EditorWidgets::drawTooltip( prop._metadata->_tooltip.c_str() );
            }
            void drawReadOnlyText( const PropertyInfo& prop, const utf8* pValue )
            {
                ImGui::TextDisabled( "%s", _pLabel );
                ImGui::SameLine();
                ImGui::TextUnformatted( pValue != nullptr ? pValue : "" );
                showTooltipIfHovered( prop );
            }
            /** @brief 내장 타입 값을 테이블의 형식(`InspectorBuiltinValue::_pFormatValue`)으로 읽기 전용 표시합니다. */
            void drawReadOnlyValue( const PropertyInfo& prop, const ValueAddress value )
            {
                fixed_string<constant::kMaxBuffer512> buf;
                const InspectorBuiltinValue*          pRow = InspectorBuiltinValueUtil::findBuiltin( prop._typeName.c_str() );
                if ( pRow != nullptr )
                    pRow->_pFormatValue( value.get(), buf.data(), buf.capacity() );
                drawReadOnlyText( prop, buf.c_str() );
            }
            bool isColorRequested( const PropertyInfo& prop ) { return InspectorPropertyLayout::isColorRequested( prop ); }
            /** @brief 색 편집 플래그입니다. `ColorHdr` 이면 1 을 넘는 값을 받습니다. */
            ImGuiColorEditFlags getColorFlags( const PropertyInfo& prop )
            {
                return ImGuiColorEditFlags_Float | ( prop._metadata->_bColorHdr != SW_FALSE ? ImGuiColorEditFlags_HDR : ImGuiColorEditFlags_None );
            }

            /** @brief 비트필드는 포인터를 잡을 수 없으므로 값으로 읽고 씁니다. 정수 계열과 bool 이 같은 체크박스를 씁니다. */
            bool drawBitFieldCheckbox( const MutableObjectAddress instance, const PropertyInfo& prop )
            {
                bool bValue = prop.getValue<bool>( instance.get() );
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, bValue ? "true" : "false" );
                    return true;
                }
                if ( ImGui::Checkbox( _pLabel, &bValue ) )
                    prop.setValue<bool>( instance.get(), bValue );
                showTooltipIfHovered( prop );
                return true;
            }

            /**
             * @brief 애셋 경로 필드의 틀(입력란, 드롭 대상, 지우기 버튼)을 그립니다.
             * @details 입력란만 호출하는 쪽이 그린다. `string` 은 키 입력마다 반영하고, `hashed_string` 은 Enter 로 확정한다.
             *          키 입력마다 인턴하면 그 문자열이 모두 테이블에 남기 때문이다. 드롭과 지우기의 처리도 호출하는 쪽이 한다.
             */
            template <typename DrawInputFn>
            AssetFieldAction drawAssetPathField( DrawInputFn&& drawInput, const PropertyInfo& prop, string_view currentPath, string& outDroppedPath )
            {
                const float32    kButtonWidth = ImGui::GetFrameHeight();
                AssetFieldAction action       = AssetFieldAction::None;

                ImGui::PushID( _pLabel );
                const float32 itemWidth    = ImGui::CalcItemWidth();
                const float32 buttonsWidth = ( kButtonWidth + ImGui::GetStyle().ItemSpacing.x ) * 3.0f;
                ImGui::SetNextItemWidth( ( itemWidth > buttonsWidth + 10.0f ) ? itemWidth - buttonsWidth : itemWidth );

                drawInput( "##assetInput" );

                if ( EditorWidgets::acceptAssetDrop( outDroppedPath ) )
                    action = AssetFieldAction::Dropped;

                // 선택 팝업이다(언리얼의 에셋 피커, 유니티의 Object Picker). 프로퍼티의 FileFilter 에 맞는 리소스만 보여 주고 검색으로 거른다.
                ImGui::SameLine();
                if ( ImGui::Button( editoricon::kSearch, ImVec2( kButtonWidth, 0 ) ) )
                {
                    AssetPickerState& picker = getAssetPickerState();
                    picker._listEntry.clear();
                    picker._search.clear();
                    EditorResourceIndex::collectEntries( picker._listEntry );
                    ImGui::OpenPopup( "AssetPicker" );
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "PickAsset" ) );
                EditorSelfTestMarks::note( ( string( "inspector.assetPicker." ) + prop._name.c_str() ).c_str() );
                if ( ImGui::BeginPopup( "AssetPicker" ) )
                {
                    AssetPickerState& picker = getAssetPickerState();
                    if ( ImGui::IsWindowAppearing() )
                        ImGui::SetKeyboardFocusHere();
                    ImGui::SetNextItemWidth( 320.0f * EditorThemeUtil::getDpiScale() );
                    const bool bEnter = ImGui::InputTextWithHint( "##pickerSearch", SW_LOCKEY( "EditorInspector", "SearchAssetsHint" ), picker._search.data(), picker._search.capacity(),
                                                                  ImGuiInputTextFlags_EnterReturnsTrue );
                    EditorSelfTestMarks::note( "inspector.assetPicker.search" );
                    const EditorListFilter filter{ picker._search.c_str() };
                    uint32                 shownCount{ 0 };
                    ImGui::BeginChild( "##pickerList", ImVec2{ 320.0f * EditorThemeUtil::getDpiScale(), 260.0f * EditorThemeUtil::getDpiScale() } );
                    for ( const EditorResourceIndexEntry& entry : picker._listEntry )
                    {
                        if ( InspectorPropertyLayout::matchesFileFilter( prop._metadata->_fileFilter, entry._path ) == false ||
                             filter.matchesAny( { string_view{ entry._path }, string_view{ entry._title } } ) == false )
                            continue;
                        const bool bFirst = shownCount == 0;
                        ++shownCount;
                        if ( ImGui::Selectable( entry._path.c_str(), entry._path == currentPath ) || ( bFirst && bEnter ) )
                        {
                            outDroppedPath = entry._path;
                            action         = AssetFieldAction::Dropped;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if ( shownCount == 0 )
                        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInspector", "NoMatchingAssets" ) );
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }

                // 콘텐츠 브라우저에서 보기. 언리얼의 Browse to Asset 에 해당한다
                ImGui::SameLine();
                ImGui::BeginDisabled( currentPath.empty() );
                if ( ImGui::Button( editoricon::kFolderOpen, ImVec2( kButtonWidth, 0 ) ) )
                    revealInContentBrowser( currentPath );
                ImGui::EndDisabled();
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ShowInContentBrowser" ) );

                ImGui::SameLine();
                if ( ImGui::Button( "x##clear", ImVec2( kButtonWidth, 0 ) ) )
                    action = AssetFieldAction::Cleared;
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInspector", "ClearAssetReference" ) );

                ImGui::PopID();
                return action;
            }

            /** @brief 리소스 id 의 폴더를 콘텐츠 브라우저로 엽니다. */
            static void revealInContentBrowser( string_view resourcePath )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr || pContext->getSubsystem<EditorPanelManager>().setPanelOpen<ContentBrowserPanel>( true ) == false )
                    return;
                ContentBrowserPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
                if ( pPanel == nullptr )
                    return;
                const string fileAbs = ResourcePaths::makeAbsolutePath( FileUtil::normalizePath( resourcePath ) );
                if ( fileAbs.empty() )
                    return;
                pPanel->openFolder( FileUtil::getDirectoryPart( fileAbs ) );
            }
        };

        // ------------------------------------------------------------------------------
        // 값 위젯. 프로퍼티와 CallInEditor 인자를 같은 함수로 그린다. 분기는 `InspectorWidgetFor<T>` 가 정한다
        // ------------------------------------------------------------------------------
        static_assert( ImGuiDataType_S8 + 1 == ImGuiDataType_U8 && ImGuiDataType_U8 + 1 == ImGuiDataType_S16 &&
                           ImGuiDataType_S16 + 2 == ImGuiDataType_S32 && ImGuiDataType_S32 + 2 == ImGuiDataType_S64 &&
                           ImGuiDataType_S64 + 1 == ImGuiDataType_U64,
                       "getNumberDataType assumes ImGuiDataType orders S8 U8 S16 U16 S32 U32 S64 U64" );

        /** @brief 숫자 타입에 대응하는 ImGui 데이터 타입입니다. 정수는 크기와 부호로 정합니다. */
        template <typename T>
        constexpr ImGuiDataType getNumberDataType()
        {
            if constexpr ( std::is_same_v<T, float32> )
                return ImGuiDataType_Float;
            else if constexpr ( std::is_same_v<T, float64> )
                return ImGuiDataType_Double;
            else
            {
                static_assert( std::is_integral_v<T> && sizeof( T ) <= 8, "Number widget needs an integer or float type" );
                constexpr int32 kSizeRank = sizeof( T ) == 1 ? 0 : ( sizeof( T ) == 2 ? 1 : ( sizeof( T ) == 4 ? 2 : 3 ) );
                return static_cast<ImGuiDataType>( ImGuiDataType_S8 + kSizeRank * 2 + ( std::is_unsigned_v<T> ? 1 : 0 ) );
            }
        }

        /** @brief 숫자 위젯의 서식입니다. 정수는 ImGui 의 타입별 서식(`%lld` 등)을 쓰고, 실수는 소수 둘째 자리까지 표시합니다. */
        template <typename T>
        const utf8* getNumberFormat()
        {
            if constexpr ( std::is_floating_point_v<T> )
                return "%.2f";
            else
                return ImGui::DataTypeGetInfo( getNumberDataType<T>() )->PrintFmt;
        }

        /** @brief Enter 로 확정하는 입력란입니다. 키 입력마다 인턴하지 않으려는 용도입니다. 확정했으면 true 입니다. */
        bool drawNameInput( const utf8* pID, hashed_string& value )
        {
            fixed_string<constant::kMaxBuffer256> buf{ value.c_str() };
            if ( ImGui::InputText( pID, buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) == false )
                return false;
            value = hashed_string( buf.c_str() );
            return true;
        }

        /**
         * @brief 내장 값 하나를 편집하는 위젯입니다. 메타데이터(범위, 단위, 색, 애셋 경로)를 모르는 기본 형태입니다.
         * @details CallInEditor 인자 입력란은 항상 이 위젯을 쓰고, 메타데이터가 필요 없는 분기(행렬, 회전, 핸들, 태그)의 프로퍼티도
         *          이 위젯을 쓴다. 분기가 늘었는데 여기에 분기가 없으면 컴파일되지 않는다.
         * @return 값이 바뀌었으면 true 입니다.
         */
        template <typename T>
        bool drawValueWidget( const utf8* pLabel, T& value )
        {
            constexpr InspectorValueWidget kWidget = InspectorWidgetFor<T>::kWidget;
            if constexpr ( kWidget == InspectorValueWidget::Number )
                return ImGui::DragScalar( pLabel, getNumberDataType<T>(), &value, std::is_integral_v<T> ? 1.0f : 0.01f, nullptr, nullptr, getNumberFormat<T>() );
            else if constexpr ( kWidget == InspectorValueWidget::Checkbox )
            {
                if constexpr ( std::is_same_v<T, bool> )
                    return ImGui::Checkbox( pLabel, &value );
                else
                {
                    bool bValue = value.load( std::memory_order_relaxed );
                    if ( ImGui::Checkbox( pLabel, &bValue ) == false )
                        return false;
                    value.store( bValue, std::memory_order_relaxed );
                    return true;
                }
            }
            else if constexpr ( kWidget == InspectorValueWidget::Text )
                return EditorWidgets::drawTextField( pLabel, value );
            else if constexpr ( kWidget == InspectorValueWidget::Name )
                return drawNameInput( pLabel, value );
            else if constexpr ( kWidget == InspectorValueWidget::Vector2 )
                return ImGui::DragFloat2( pLabel, &value._x, 0.1f );
            else if constexpr ( kWidget == InspectorValueWidget::Vector3 )
                return ImGui::DragFloat3( pLabel, &value._x, 0.1f );
            else if constexpr ( kWidget == InspectorValueWidget::Vector4 )
                return ImGui::DragFloat4( pLabel, &value._x, 0.01f );
            else if constexpr ( kWidget == InspectorValueWidget::Matrix )
            {
                // 행 네 개를 한 그룹으로 둔다. 되돌리기 추적이 마지막 항목(그룹)을 본다.
                bool bChanged = false;
                ImGui::PushID( pLabel );
                ImGui::BeginGroup();
                for ( int32 rowIndex = 0; rowIndex < 4; ++rowIndex )
                {
                    ImGui::PushID( rowIndex );
                    bChanged |= ImGui::DragFloat4( "##row", value.data() + rowIndex * 4, 0.01f );
                    ImGui::PopID();
                }
                ImGui::EndGroup();
                ImGui::PopID();
                return bChanged;
            }
            else if constexpr ( kWidget == InspectorValueWidget::Rotation )
            {
                float3 degrees = value.getEulerAngles() * MathUtil::kRadianToDegree;
                if ( ImGui::DragFloat3( pLabel, &degrees._x, 0.5f, 0.0f, 0.0f, "%.1f deg" ) == false )
                    return false;
                value = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
                return true;
            }
            else if constexpr ( kWidget == InspectorValueWidget::Handle )
            {
                if constexpr ( std::is_same_v<T, GameObjectHandle> )
                {
                    uint64 objectID = value.objectID();
                    if ( ImGui::InputScalar( pLabel, ImGuiDataType_U64, &objectID ) == false )
                        return false;
                    value = GameObjectHandle::make( objectID );
                    return true;
                }
                else if constexpr ( std::is_same_v<T, ComponentHandle> )
                {
                    uint64 arrID[2] = { value.objectID(), value.componentID() };
                    if ( ImGui::InputScalarN( pLabel, ImGuiDataType_U64, arrID, 2 ) == false )
                        return false;
                    value = ComponentHandle::makeOwned( arrID[0], arrID[1] );
                    return true;
                }
                else
                {
                    static_assert( std::is_same_v<T, SlotHandle>, "Handle widget: unknown handle type" );
                    uint32 arrPart[2] = { value.index(), value.generation() };
                    if ( ImGui::InputScalarN( pLabel, ImGuiDataType_U32, arrPart, 2 ) == false )
                        return false;
                    value = SlotHandle::make( arrPart[0], arrPart[1] );
                    return true;
                }
            }
            else
            {
                static_assert( kWidget == InspectorValueWidget::Tag, "drawValueWidget: no widget for this InspectorValueWidget" );
                fixed_string<constant::kMaxBuffer256> buf{ value.isValid() ? value.getString() : "" };
                if ( ImGui::InputText( pLabel, buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) == false )
                    return false;
                value = buf.empty() ? TagID{} : TagID::request( buf.c_str() );
                return true;
            }
        }

        /** @brief CallInEditor 인자 입력란 하나를 그 C++ 타입의 위젯으로 그립니다. `ReflectBuiltins.xxx` 항목마다 하나씩 있습니다. */
        template <typename T>
        bool drawMethodArgFor( const utf8* pLabel, TaskValue& value )
        {
            T edited = value.getValue<T>();
            if ( drawValueWidget( pLabel, edited ) == false )
                return false;
            value = TaskValue{ std::move( edited ) };
            return true;
        }

        using DrawMethodArgFn = bool ( * )( const utf8* pLabel, TaskValue& value );

        /** @brief 인자 위젯 테이블입니다. 순서는 `InspectorBuiltinValueUtil` 테이블과 같습니다. 같은 파일을 같은 순서로 include 합니다. */
        const DrawMethodArgFn kArrDrawMethodArg[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) &drawMethodArgFor<InspectorBuiltinCppTypeT<CppType>>,
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
        };

        // ------------------------------------------------------------------------------
        // 숫자 프로퍼티. 정수 여덟 개와 실수 두 개가 같은 경로로 그리고, 타입마다 다른 것은 ImGui 데이터 타입뿐이다
        // ------------------------------------------------------------------------------
        template <typename T>
        class NumericProperty : public BuiltinPropertyBase
        {
            static constexpr bool kIsInteger = std::is_integral_v<T>;

        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                if constexpr ( kIsInteger )
                {
                    if ( prop._bIsBitField == SW_TRUE )
                        return drawBitFieldCheckbox( instance, prop );
                }

                T* pPtr = prop.getValuePtr<T>( instance.get() );
                if ( pPtr == nullptr )
                    return true;

                // 표시 단위(라디안에서 도)의 배율은 실수에만 적용한다. 정수는 단위를 문자열로만 붙인다.
                const InspectorDisplayUnit unit      = InspectorPropertyLayout::getDisplayUnit( prop );
                const float64              scale     = kIsInteger ? 1.0 : static_cast<float64>( unit._scale );
                const float32              dragSpeed = kIsInteger ? 1.0f : ( unit._dragSpeed > 0.0f ? unit._dragSpeed : 0.01f );
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    if constexpr ( kIsInteger )
                        formatstring( buf.data(), buf.capacity(), "%#", *pPtr );
                    else
                        formatstring( buf.data(), buf.capacity(), "%#", static_cast<float64>( *pPtr ) * scale );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }

                // 위젯은 `UIMin` 과 `UIMax`(없으면 `Min` 과 `Max`) 안에서 움직이고, 값은 항상 `Min` 과 `Max` 로 제한한다(`InspectorNumericRange`).
                // 적히지 않은 쪽은 nullptr 이므로 ImGui 가 타입의 범위로 제한한다(uint8 은 0..255). 필드 이름 형태 경계는 이 객체의 그 필드 값이다.
                const InspectorNumericRange range    = InspectorPropertyLayout::getNumericRange( prop, instance );
                const T                     minValue = static_cast<T>( range._widgetMin * scale );
                const T                     maxValue = static_cast<T>( range._widgetMax * scale );
                const T*                    pMin     = range._bHasWidgetMin ? &minValue : nullptr;
                const T*                    pMax     = range._bHasWidgetMax ? &maxValue : nullptr;
                const bool                  bSlider  = range._bSlider;
                const string                fmt      = InspectorPropertyLayout::appendUnitSuffix( getNumberFormat<T>(), unit._suffix );

                T    widgetValue = kIsInteger ? *pPtr : static_cast<T>( static_cast<float64>( *pPtr ) * scale );
                bool bChanged    = false;
                if ( bSlider )
                    bChanged = ImGui::SliderScalar( _pLabel, getNumberDataType<T>(), &widgetValue, pMin, pMax, fmt.c_str() );
                else
                    bChanged = ImGui::DragScalar( _pLabel, getNumberDataType<T>(), &widgetValue, dragSpeed, pMin, pMax, fmt.c_str() );
                if ( bChanged )
                {
                    const float64 stored = kIsInteger ? static_cast<float64>( widgetValue ) : static_cast<float64>( widgetValue ) / scale;
                    *pPtr                = static_cast<T>( InspectorPropertyLayout::clampToAllowedRange( range, stored ) );
                }
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        // ------------------------------------------------------------------------------
        // 그 밖의 내장 타입
        // ------------------------------------------------------------------------------
        /** @brief bool 과 atomic<bool> 의 체크박스입니다. 정수 비트필드와 같은 체크박스를 씁니다. */
        template <typename T>
        class CheckboxProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                if constexpr ( std::is_same_v<T, bool> )
                {
                    if ( prop._bIsBitField == SW_TRUE )
                        return drawBitFieldCheckbox( instance, prop );
                }

                T* pPtr = prop.getValuePtr<T>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    drawReadOnlyValue( prop, ValueAddress{ pPtr } );
                    return true;
                }
                drawValueWidget( _pLabel, *pPtr );
                showTooltipIfHovered( prop );
                // atomic<bool> 은 다른 스레드가 쓰는 값이므로 되돌리기 기록에 넣지 않는다.
                if constexpr ( std::is_same_v<T, bool> )
                    InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        /** @brief 메타데이터가 필요 없는 분기(행렬, 회전, 핸들, 태그)의 프로퍼티입니다. 편집 위젯은 인자 입력란과 같습니다. */
        template <typename T>
        class ValueProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                T* pPtr = prop.getValuePtr<T>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    drawReadOnlyValue( prop, ValueAddress{ pPtr } );
                    return true;
                }
                drawValueWidget( _pLabel, *pPtr );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        /**
         * @brief 컴포넌트 핸들의 위젯입니다. 대상 타입이 있는 핸들(`TComponentHandle<T>`)은 선택기만 그리고, 없는 핸들은 `ValueProperty` 와 같이 그립니다.
         * @details 선택기가 쓰는 값은 **열거한 컴포넌트의 `getHandle()`** 뿐이라 타입이 틀린 값이 들어갈 수 없다(`docs/09_Decisions.md` 5-6 타입 핸들 인스펙터).
         *          대상 타입, 레지스트리, 편집 대상의 씬 중 하나라도 없으면 선택기를 그리지 않고 오류 글과 한 번의 로그로 멈춘다. 활성 씬이나 모든 컴포넌트로
         *          넓히는 것은 폴백이다.
         */
        class ComponentHandleProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                if ( prop._handleTargetTypeName.empty() )
                    return _untyped.draw( instance, prop, grid );

                const TypeRegistry*      pRegistry   = findSubsystem<TypeRegistry>();
                const TypeInfo*          pTargetType = pRegistry != nullptr ? InspectorComponentPickerUtil::findTargetType( *pRegistry, prop ) : nullptr;
                const GameObject*        pOwner      = grid.getEditOwner();
                const GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
                if ( pTargetType == nullptr || pManager == nullptr )
                {
                    drawUnavailable( prop, pRegistry != nullptr && pTargetType == nullptr );
                    return true;
                }

                ComponentHandle* pValue = prop.getValuePtr<ComponentHandle>( instance.get() );
                if ( pValue == nullptr )
                    return true;

                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    const InspectorHandleState            state = InspectorComponentPickerUtil::computeHandleState( *pManager, *pTargetType, *pValue );
                    fixed_string<constant::kMaxBuffer512> preview;
                    makePreview( *pManager, *pValue, state, preview );
                    drawReadOnlyText( prop, preview.c_str() );
                    return true;
                }

                ComponentHandle picked{};
                // 쓰기는 그리드 한곳(통지 · 되돌리기)이 맡는다. Selectable 은 ImGui 편집 플래그를 세우지 않으므로 trackPod 도 부르지 않는다.
                if ( drawPicker( _pLabel, *pManager, *pTargetType, *pValue, _listChoice, picked ) )
                {
                    // `picked` 는 값 자체라 값 주소 쪽 변환(`formatValueText`)으로 쓴다. 객체 주소 쪽(`formatPropertyText`)은 오프셋을 한 번 더 더한다.
                    const string undoLabel = InspectorPropertyLayout::makeUndoLabel( InspectorPropertyLayout::getPropertyLabel( prop ) );
                    grid.applyPropertyTextAsEdit( instance, prop,
                                                  SerializerUtil::formatValueText( ValueAddress{ &picked }, prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) ),
                                                  undoLabel.c_str() );
                }
                showTooltipIfHovered( prop );
                return true;
            }

            /**
             * @brief 선택기 콤보 하나입니다. 프로퍼티와 CallInEditor 인자(`InspectorPropertyManager::drawMethodArg`)가 같은 것을 씁니다.
             * @details 고를 수 있는 값은 "(none)" 과 @p targetType 의 열거한 컴포넌트 핸들뿐이다. 목록은 콤보가 열린 프레임에만 @p inoutListChoice 에 모은다.
             * @return 지금 값과 다른 것을 골랐으면 true 이고 @p outPicked 에 담습니다.
             */
            static bool drawPicker( const utf8* pLabel, const GameObjectManager& manager, const TypeInfo& targetType, ComponentHandle current,
                                    vector<InspectorComponentChoice>& inoutListChoice, ComponentHandle& outPicked )
            {
                const InspectorHandleState            state = InspectorComponentPickerUtil::computeHandleState( manager, targetType, current );
                fixed_string<constant::kMaxBuffer512> preview;
                makePreview( manager, current, state, preview );
                const bool bColored = state == InspectorHandleState::Missing || state == InspectorHandleState::WrongType;
                if ( bColored )
                    EditorThemeUtil::pushTextColor( state == InspectorHandleState::Missing ? EditorThemeUtil::getWarningColor() : EditorThemeUtil::getErrorColor() );
                const bool bOpen = ImGui::BeginCombo( pLabel, preview.c_str() );
                if ( bColored )
                    EditorThemeUtil::popTextColor();
                if ( bOpen == false )
                    return false;
                // 열린 프레임에만 모은다. 닫혀 있을 때 비용은 0 이다.
                InspectorComponentPickerUtil::collectChoices( manager, targetType, inoutListChoice );
                ComponentHandle picked{};
                bool            bPicked = false;
                if ( ImGui::Selectable( "(none)", current.isValid() == false ) )
                    bPicked = true;
                EditorSelfTestMarks::note( "inspector.handle.none" ); // 시나리오가 누른다 — 콤보를 연 프레임에만 그려진다
                if ( inoutListChoice.empty() )
                    ImGui::TextDisabled( "No %s in this scene", targetType._name.c_str() );
                for ( const InspectorComponentChoice& choice : inoutListChoice )
                {
                    if ( ImGui::Selectable( choice._label.c_str(), choice._handle == current ) )
                    {
                        picked  = choice._handle;
                        bPicked = true;
                    }
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::note( ( "inspector.handle.choice." + choice._label ).c_str() );
                }
                ImGui::EndCombo();
                if ( bPicked == false || picked == current )
                    return false;
                outPicked = picked;
                return true;
            }

        private:
            /** @brief 현재 값의 미리보기 글입니다. */
            static void makePreview( const GameObjectManager& manager, ComponentHandle value, InspectorHandleState state, fixed_string<constant::kMaxBuffer512>& outPreview )
            {
                switch ( state )
                {
                    case InspectorHandleState::Empty:
                    {
                        formatstring( outPreview.data(), outPreview.capacity(), "(none)" );
                        return;
                    }
                    case InspectorHandleState::Missing:
                    {
                        formatstring( outPreview.data(), outPreview.capacity(), "(missing) object %# / component %#", value.objectID(), value.componentID() );
                        return;
                    }
                    case InspectorHandleState::Valid:
                    case InspectorHandleState::WrongType:
                    {
                        break;
                    }
                }
                const Component* pComp = manager.resolveComponent( value );
                const string     label = pComp != nullptr ? InspectorComponentPickerUtil::makeLabel( *pComp ) : string();
                formatstring( outPreview.data(), outPreview.capacity(), "%#%#", state == InspectorHandleState::WrongType ? "(wrong type) " : "", label );
            }

            /** @brief 선택기를 그릴 수 없을 때의 오류 글입니다. 필드마다 한 번만 로그를 남깁니다. */
            void drawUnavailable( const PropertyInfo& prop, bool bUnknownType )
            {
                fixed_string<constant::kMaxBuffer512> text;
                if ( bUnknownType )
                    formatstring( text.data(), text.capacity(), "(unknown component type '%#')", prop._handleTargetTypeName.c_str() );
                else
                    formatstring( text.data(), text.capacity(), "(no scene: cannot pick a component)" );
                ImGui::TextDisabled( "%s", _pLabel );
                ImGui::SameLine();
                EditorThemeUtil::textError( text.c_str() );
                if ( _uniqueReported.insert( static_cast<uint32>( prop._name.getHash() ) ).second )
                    SW_LOG_ERROR( "Inspector: component handle '%#' cannot be edited: %#", prop._name.c_str(), text.c_str() );
                showTooltipIfHovered( prop );
            }

            ValueProperty<ComponentHandle>   _untyped;
            vector<InspectorComponentChoice> _listChoice;     ///< 콤보가 열린 프레임에만 채운다
            unordered_set<uint32>            _uniqueReported; ///< 오류를 이미 남긴 필드(이름 해시)
        };

        class StringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                const bool bAssetPath = prop._metadata->_bAssetPath != SW_FALSE || prop._metadata->_assetType.empty() == false;

                string* pPtr = prop.getValuePtr<string>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, pPtr->c_str() );
                    return true;
                }

                if ( bAssetPath )
                {
                    string                 droppedPath;
                    const AssetFieldAction action = drawAssetPathField(
                        [pPtr]( const utf8* pID )
                    { EditorWidgets::drawTextField( pID, *pPtr ); }, prop, string_view{ *pPtr }, droppedPath );
                    // `FileFilter` 에 맞지 않는 파일은 받지 않는다. 드롭, 선택, 지우기는 위젯 편집이 아니라 그리드가 적용해야 알림과 되돌리기에 남는다.
                    if ( action == AssetFieldAction::Dropped && InspectorPropertyLayout::matchesFileFilter( prop._metadata->_fileFilter, droppedPath ) )
                        grid.applyPropertyTextAsEdit( instance, prop, droppedPath, "Set Asset" );
                    else if ( action == AssetFieldAction::Cleared )
                        grid.applyPropertyTextAsEdit( instance, prop, "", "Clear Asset" );
                }
                else if ( prop._metadata->_bMultiline != SW_FALSE )
                {
                    EditorWidgets::drawTextFieldMultiline( _pLabel, *pPtr );
                }
                else
                {
                    EditorWidgets::drawTextField( _pLabel, *pPtr );
                }
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackString( pPtr, prop );
                return true;
            }
        };

        class HashedStringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                const bool bAssetPath = prop._metadata->_bAssetPath != SW_FALSE || prop._metadata->_assetType.empty() == false;

                hashed_string* pPtr = prop.getValuePtr<hashed_string>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, pPtr->c_str() );
                    return true;
                }

                if ( bAssetPath )
                {
                    string                 droppedPath;
                    const AssetFieldAction action = drawAssetPathField(
                        [pPtr]( const utf8* pID )
                    { drawNameInput( pID, *pPtr ); }, prop, pPtr->view(), droppedPath );
                    if ( action == AssetFieldAction::Dropped && InspectorPropertyLayout::matchesFileFilter( prop._metadata->_fileFilter, droppedPath ) )
                        grid.applyPropertyTextAsEdit( instance, prop, droppedPath, "Set Asset" );
                    else if ( action == AssetFieldAction::Cleared )
                        grid.applyPropertyTextAsEdit( instance, prop, "", "Clear Asset" );
                }
                else
                {
                    drawNameInput( _pLabel, *pPtr );
                }
                showTooltipIfHovered( prop );
                // 인턴 인덱스 하나이므로 POD 로 되돌린다. 인턴된 문자열은 해제되지 않으므로 이전 인덱스는 언제나 유효하다.
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float3Property : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float3* pPtr = prop.getValuePtr<float3>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                // 라디안으로 저장한 각도(`Units=rad`)는 도로 보여 주고 편집한다. 바뀌었을 때만 다시 쓴다.
                const InspectorDisplayUnit unit = InspectorPropertyLayout::getDisplayUnit( prop );
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    const float3                          shown = *pPtr * unit._scale;
                    fixed_string<constant::kMaxBuffer128> buf;
                    formatstring( buf.data(), buf.capacity(), "(%#, %#)",
                                  Fmt( shown._x, Format( 2 ) ), Fmt( shown._y, Format( 2 ) ), Fmt( shown._z, Format( 2 ) ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                if ( isColorRequested( prop ) )
                    ImGui::ColorEdit3( _pLabel, &pPtr->_x, getColorFlags( prop ) );
                else if ( unit._scale != 1.0f )
                {
                    float3 shown = *pPtr * unit._scale;
                    if ( EditorWidgets::drawVec3Control( _pLabel, shown, 0.0f, 100.0f, unit._dragSpeed ) )
                        *pPtr = shown * ( 1.0f / unit._scale );
                }
                else
                    EditorWidgets::drawVec3Control( _pLabel, *pPtr, 0.0f, 100.0f, 0.1f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float2Property : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float2* pPtr = prop.getValuePtr<float2>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer128> buf;
                    formatstring( buf.data(), buf.capacity(), "(%#, %#)",
                                  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                ImGui::DragFloat2( _pLabel, &pPtr->_x, 0.1f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float4Property : public BuiltinPropertyBase
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float4* pPtr = prop.getValuePtr<float4>( instance.get() );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata->_bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer256> buf;
                    formatstring( buf.data(), buf.capacity(), "(%#, %#, %#, %#)",
                                  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ),
                                  Fmt( pPtr->_z, Format( 2 ) ), Fmt( pPtr->_w, Format( 2 ) ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                if ( isColorRequested( prop ) )
                    ImGui::ColorEdit4( _pLabel, &pPtr->_x, getColorFlags( prop ) );
                else
                    ImGui::DragFloat4( _pLabel, &pPtr->_x, 0.01f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( MutableValueAddress{ pPtr }, sizeof( *pPtr ), prop );
                return true;
            }
        };

        /**
         * @brief 내장 타입 하나의 프로퍼티 위젯을 만듭니다. 분기는 `InspectorWidgetFor<T>` 가 정합니다.
         * @details 메타데이터(범위, 단위, 색, 애셋 경로)를 읽는 분기는 전용 클래스를 쓰고, 나머지는 `ValueProperty` 를 씁니다.
         */
        template <typename T>
        unique_ptr<IInspectorProperty> createBuiltinProperty()
        {
            constexpr InspectorValueWidget kWidget = InspectorWidgetFor<T>::kWidget;
            if constexpr ( std::is_same_v<T, ComponentHandle> )
                return make_unique<ComponentHandleProperty>();
            else if constexpr ( kWidget == InspectorValueWidget::Number )
                return make_unique<NumericProperty<T>>();
            else if constexpr ( kWidget == InspectorValueWidget::Checkbox )
                return make_unique<CheckboxProperty<T>>();
            else if constexpr ( kWidget == InspectorValueWidget::Text )
                return make_unique<StringProperty>();
            else if constexpr ( kWidget == InspectorValueWidget::Name )
                return make_unique<HashedStringProperty>();
            else if constexpr ( kWidget == InspectorValueWidget::Vector2 )
                return make_unique<Float2Property>();
            else if constexpr ( kWidget == InspectorValueWidget::Vector3 )
                return make_unique<Float3Property>();
            else if constexpr ( kWidget == InspectorValueWidget::Vector4 )
                return make_unique<Float4Property>();
            else
                return make_unique<ValueProperty<T>>();
        }

        static_assert( std::size( kArrDrawMethodArg ) == InspectorBuiltinValueUtil::kBuiltinCount,
                       "kArrDrawMethodArg must have one entry per ReflectBuiltins.xxx type" );

        /** @brief 메서드 인자 선택기 콤보의 선택지 버퍼를 담은 상태입니다. 에디터 범위의 상태 저장소가 소유한다. */
        struct InspectorMethodArgChoiceState
        {
            vector<InspectorComponentChoice> _listChoice;
        };
    } // namespace

    InspectorPropertyManager::InspectorPropertyManager()
        : _registry{}
        , _listRegistration{}
        , _syncedGeneration{ invalid_index::kUint32 }
    {
    }

    InspectorPropertyManager::~InspectorPropertyManager() = default;

    void InspectorPropertyManager::registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty )
    {
        _registry.addOrReplace( hashed_string( typeName ), std::move( pProperty ) );
    }

    IInspectorProperty* InspectorPropertyManager::find( string_view typeName ) const
    {
        const unique_ptr<IInspectorProperty>* pProperty = _registry.find( hashed_string( typeName ) );
        return pProperty != nullptr ? pProperty->get() : nullptr;
    }

    void InspectorPropertyManager::registerDefaults()
    {
        registerBuiltins();
        _listRegistration.clear();
        _syncedGeneration = invalid_index::kUint32;
        syncWithRegistry();
    }

    void InspectorPropertyManager::syncWithRegistry()
    {
        using DrawerRegistry                        = EditorExtensionRegistry<EditorPropertyDrawerRegistration>;
        const EditorExtensionRegistrationList& list = DrawerRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();
        // 항목이 빠졌으면 내장 위젯부터 다시 등록한다. 등록 항목이 가렸던 것이 돌아온다. 그다음 현재 항목을 모두 등록한다.
        bool bRemoved = false;
        for ( const EditorPropertyDrawerRegistration* pRegistration : _listRegistration )
        {
            bRemoved = bRemoved || DrawerRegistry::find( pRegistration->_pID ) != pRegistration;
        }
        if ( bRemoved )
        {
            _registry.clear();
            registerBuiltins();
        }
        _listRegistration.clear();
        for ( uint32 index = 0; index < DrawerRegistry::getCount(); ++index )
        {
            const EditorPropertyDrawerRegistration& registration = DrawerRegistry::getAt( index );
            registerType( registration._pID, registration._pCreate() );
            _listRegistration.push_back( &registration );
        }
    }

    uint32 InspectorPropertyManager::releaseDrawersWithin( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listRegistration.size(); index-- > 0; )
        {
            const EditorPropertyDrawerRegistration* pRegistration = _listRegistration[index];
            if ( IModuleUnloadListener::isAddressWithin( pRegistration, pBegin, pEnd ) == false )
                continue;
            _listRegistration.erase( _listRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            ++releasedCount;
        }
        if ( releasedCount > 0 )
        {
            _registry.clear();  // 언로드되는 이미지의 위젯(그 vtable)을 남기지 않는다
            registerBuiltins(); // 가려졌던 내장 위젯을 복원한다
            for ( const EditorPropertyDrawerRegistration* pRegistration : _listRegistration )
            {
                registerType( pRegistration->_pID, pRegistration->_pCreate() );
            }
        }
        return releasedCount;
    }

    void InspectorPropertyManager::registerBuiltins()
    {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) registerType( #Canon, createBuiltinProperty<InspectorBuiltinCppTypeT<CppType>>() );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
    }

    bool InspectorPropertyManager::drawMethodArg( const utf8* pLabel, InspectorMethodArgSlot& slot, const FunctionParameterInfo& parameter, const GameObjectManager* pManager )
    {
        if ( slot._value.hasValue() == false || slot._builtinIndex >= std::size( kArrDrawMethodArg ) )
        {
            ImGui::TextDisabled( "%s (unsupported in UI)", pLabel );
            return false;
        }
        if ( parameter._handleTargetTypeName.empty() )
            return kArrDrawMethodArg[slot._builtinIndex]( pLabel, slot._value );

        // 타입 핸들 인자 — 프로퍼티와 같은 선택기다. 칸의 값은 타입 없는 핸들이고(`_typeName` 이 `ComponentHandle`), 호출 썽크가 대상 타입을 다시 확인한다.
        const TypeRegistry* pRegistry   = findSubsystem<TypeRegistry>();
        const TypeInfo*     pTargetType = pRegistry != nullptr ? InspectorComponentPickerUtil::findTargetType( *pRegistry, parameter._handleTargetTypeName ) : nullptr;
        const bool          bPickable   = pTargetType != nullptr && pManager != nullptr && slot._value.getPtr<ComponentHandle>() != nullptr;
        if ( bPickable == false )
        {
            ImGui::TextDisabled( "%s", pLabel );
            ImGui::SameLine();
            EditorThemeUtil::textError( pTargetType == nullptr ? "(unknown component type)" : "(no scene: cannot pick a component)" );
            return false;
        }
        vector<InspectorComponentChoice>& listChoice = getEditorState<InspectorMethodArgChoiceState>()._listChoice; // 콤보가 열린 프레임에만 채운다(UI 스레드 하나만 그린다)
        ComponentHandle                   picked{};
        if ( ComponentHandleProperty::drawPicker( pLabel, *pManager, *pTargetType, slot._value.getValue<ComponentHandle>(), listChoice, picked ) == false )
            return false;
        slot._value = TaskValue{ picked };
        return true;
    }
} // namespace sw::editor
