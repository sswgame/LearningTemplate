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

#include "Editor/Common/Commands/EditorResourceIndex.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 애셋 경로 필드에서 이번 프레임에 일어난 일입니다. */
        enum class AssetFieldAction : uint8
        {
            None,
            Dropped, ///< 드래그 드롭이나 고르기 팝업으로 경로가 들어왔다
            Cleared, ///< 지우기 버튼
        };

        /** @brief 에셋 고르기 팝업의 상태입니다. 열린 팝업은 하나라 하나만 든다. */
        struct AssetPickerState
        {
            vector<EditorResourceIndexEntry>     _listEntry; ///< 열 때 한 번 모은 리소스(파일 목록을 프레임마다 훑지 않는다)
            fixed_string<constant::kMaxBuffer64> _search;
        };

        AssetPickerState& getAssetPickerState()
        {
            static AssetPickerState s_state;
            return s_state;
        }

        class BuiltinPropertyBase : public IInspectorProperty
        {
        protected:
            const utf8* _pLabel = "##value";
            void        showTooltipIfHovered( const PropertyInfo& prop )
            {
                EditorWidgets::drawTooltip( prop._metadata._tooltip.c_str() );
            }
            void drawReadOnlyText( const PropertyInfo& prop, const utf8* pValue )
            {
                ImGui::TextDisabled( "%s", _pLabel );
                ImGui::SameLine();
                ImGui::TextUnformatted( pValue != nullptr ? pValue : "" );
                showTooltipIfHovered( prop );
            }
            /** @brief 내장 타입 값을 표의 형식(`InspectorBuiltinValue::_pFormatValue`)으로 읽기 전용 표시합니다. */
            void drawReadOnlyValue( const PropertyInfo& prop, const void* pValue )
            {
                fixed_string<constant::kMaxBuffer512> buf;
                const InspectorBuiltinValue*          pRow = InspectorBuiltinValueUtil::findBuiltin( prop._typeName.c_str() );
                if ( pRow != nullptr )
                    pRow->_pFormatValue( pValue, buf.data(), buf.capacity() );
                drawReadOnlyText( prop, buf.c_str() );
            }
            bool isColorRequested( const PropertyInfo& prop ) { return InspectorPropertyLayout::isColorRequested( prop ); }
            /** @brief 색 편집 플래그입니다. `ColorHdr` 면 1 을 넘는 값을 받는다. */
            ImGuiColorEditFlags getColorFlags( const PropertyInfo& prop )
            {
                return ImGuiColorEditFlags_Float | ( prop._metadata._bColorHdr != SW_FALSE ? ImGuiColorEditFlags_HDR : ImGuiColorEditFlags_None );
            }

            /** @brief 비트필드는 포인터를 잡을 수 없어 값으로 읽고 씁니다. 정수 계열과 bool 이 같은 체크박스를 씁니다. */
            bool drawBitFieldCheckbox( void* pInstance, const PropertyInfo& prop )
            {
                bool bValue = prop.getValue<bool>( pInstance );
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, bValue ? "true" : "false" );
                    return true;
                }
                if ( ImGui::Checkbox( _pLabel, &bValue ) )
                    prop.setValue<bool>( pInstance, bValue );
                showTooltipIfHovered( prop );
                return true;
            }

            /**
             * @brief 애셋 경로 필드의 틀(입력 칸 · 드롭 대상 · 지우기 버튼)을 그립니다.
             * @details 입력 칸만 부르는 쪽이 그립니다. `string` 은 키 입력마다 반영하고, `hashed_string` 은 Enter 로 확정합니다
             *          (키 입력마다 인턴하면 그 문자열이 모두 표에 남습니다). 드롭과 지우기의 처리도 부르는 쪽이 합니다.
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

                // 고르기 팝업(언리얼 에셋 피커 · 유니티 Object Picker) — 프로퍼티의 FileFilter 에 맞는 리소스만, 검색으로 거른다.
                ImGui::SameLine();
                if ( ImGui::Button( editoricon::kSearch, ImVec2( kButtonWidth, 0 ) ) )
                {
                    AssetPickerState& picker = getAssetPickerState();
                    picker._listEntry.clear();
                    picker._search.clear();
                    EditorResourceIndex::collectEntries( picker._listEntry );
                    ImGui::OpenPopup( "AssetPicker" );
                }
                EditorWidgets::drawTooltip( "Pick an asset" );
                EditorSelfTestMarks::note( ( string( "inspector.assetPicker." ) + prop._name.c_str() ).c_str() );
                if ( ImGui::BeginPopup( "AssetPicker" ) )
                {
                    AssetPickerState& picker = getAssetPickerState();
                    if ( ImGui::IsWindowAppearing() )
                        ImGui::SetKeyboardFocusHere();
                    ImGui::SetNextItemWidth( 320.0f * EditorThemeUtil::getDpiScale() );
                    const bool bEnter = ImGui::InputTextWithHint( "##pickerSearch", "Search assets...", picker._search.data(), picker._search.capacity(),
                                                                  ImGuiInputTextFlags_EnterReturnsTrue );
                    EditorSelfTestMarks::note( "inspector.assetPicker.search" );
                    const EditorListFilter filter{ picker._search.c_str() };
                    uint32                 shownCount{ 0 };
                    ImGui::BeginChild( "##pickerList", ImVec2{ 320.0f * EditorThemeUtil::getDpiScale(), 260.0f * EditorThemeUtil::getDpiScale() } );
                    for ( const EditorResourceIndexEntry& entry : picker._listEntry )
                    {
                        if ( InspectorPropertyLayout::matchesFileFilter( prop._metadata._fileFilter, entry._path ) == false ||
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
                        ImGui::TextDisabled( "No matching assets." );
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }

                // 콘텐츠 브라우저에서 보기(언리얼 Browse to Asset)
                ImGui::SameLine();
                ImGui::BeginDisabled( currentPath.empty() );
                if ( ImGui::Button( editoricon::kFolderOpen, ImVec2( kButtonWidth, 0 ) ) )
                    revealInContentBrowser( currentPath );
                ImGui::EndDisabled();
                EditorWidgets::drawTooltip( "Show in the Content Browser" );

                ImGui::SameLine();
                if ( ImGui::Button( "x##clear", ImVec2( kButtonWidth, 0 ) ) )
                    action = AssetFieldAction::Cleared;
                EditorWidgets::drawTooltip( "Clear asset reference" );

                ImGui::PopID();
                return action;
            }

            /** @brief 리소스 id 의 폴더를 콘텐츠 브라우저로 엽니다. */
            static void revealInContentBrowser( string_view resourcePath )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr || pContext->getPanelManager().setPanelOpen( "content_browser", true ) == false )
                    return;
                ContentBrowserPanel* pPanel = static_cast<ContentBrowserPanel*>( pContext->getPanelManager().findPanel( "content_browser" ) );
                if ( pPanel == nullptr )
                    return;
                const string fileAbs = FileUtil::joinPath( ResourceUtil::getRootFolderPath(), FileUtil::normalizePath( resourcePath ) );
                pPanel->openFolder( FileUtil::getDirectoryPart( fileAbs ) );
            }
        };

        // ------------------------------------------------------------------------------
        // 값 위젯. 프로퍼티와 CallInEditor 인자가 같은 함수로 그린다 — 갈래는 `InspectorWidgetFor<T>` 가 정한다
        // ------------------------------------------------------------------------------
        static_assert( ImGuiDataType_S8 + 1 == ImGuiDataType_U8 && ImGuiDataType_U8 + 1 == ImGuiDataType_S16 &&
                           ImGuiDataType_S16 + 2 == ImGuiDataType_S32 && ImGuiDataType_S32 + 2 == ImGuiDataType_S64 &&
                           ImGuiDataType_S64 + 1 == ImGuiDataType_U64,
                       "getNumberDataType assumes ImGuiDataType orders S8 U8 S16 U16 S32 U32 S64 U64" );

        /** @brief 숫자 타입의 ImGui 데이터 타입입니다. 정수는 크기 · 부호로 정합니다. */
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

        /** @brief 숫자 위젯의 서식입니다. 정수는 ImGui 의 타입별 서식(`%lld` 등), 실수는 소수 둘째 자리입니다. */
        template <typename T>
        const utf8* getNumberFormat()
        {
            if constexpr ( std::is_floating_point_v<T> )
                return "%.2f";
            else
                return ImGui::DataTypeGetInfo( getNumberDataType<T>() )->PrintFmt;
        }

        /** @brief Enter 로 확정하는 입력 칸입니다. 키 입력마다 인턴하지 않으려는 것입니다. 확정했으면 true 입니다. */
        bool drawNameInput( const utf8* pID, hashed_string& value )
        {
            fixed_string<constant::kMaxBuffer256> buf{ value.c_str() };
            if ( ImGui::InputText( pID, buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) == false )
                return false;
            value = hashed_string( buf.c_str() );
            return true;
        }

        /**
         * @brief 내장 값 하나를 고치는 위젯입니다. 메타데이터(범위 · 단위 · 색 · 애셋 경로)를 모르는 기본 모양입니다.
         * @details CallInEditor 인자 칸은 늘 이것을 쓰고, 메타데이터가 필요 없는 갈래(행렬 · 회전 · 핸들 · 태그)의 프로퍼티도
         *          이것을 씁니다. 갈래가 늘었는데 여기 분기가 없으면 컴파일되지 않습니다.
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
                // 행 넷을 한 묶음으로 둔다 — 되돌리기 추적이 마지막 항목(묶음)을 본다.
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

        /** @brief CallInEditor 인자 칸 하나를 그 C++ 타입의 위젯으로 그립니다. `ReflectBuiltins.xxx` 줄마다 하나입니다. */
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

        /** @brief 인자 위젯 표입니다. 순서는 `InspectorBuiltinValueUtil` 표와 같습니다(같은 파일을 같은 순서로 include). */
        const DrawMethodArgFn kArrDrawMethodArg[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) &drawMethodArgFor<InspectorBuiltinCppTypeT<CppType>>,
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
        };

        // ------------------------------------------------------------------------------
        // 숫자 프로퍼티. 정수 여덟 · 실수 둘이 같은 경로로 그리고, 타입마다 다른 것은 ImGui 데이터 타입뿐이다
        // ------------------------------------------------------------------------------
        template <typename T>
        class NumericProperty : public BuiltinPropertyBase
        {
            static constexpr bool kIsInteger = std::is_integral_v<T>;

        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                if constexpr ( kIsInteger )
                {
                    if ( prop._bIsBitField == SW_TRUE )
                        return drawBitFieldCheckbox( pInstance, prop );
                }

                T* pPtr = prop.getValuePtr<T>( pInstance );
                if ( pPtr == nullptr )
                    return true;

                // 보이는 단위(라디안 → 도)의 배율은 실수에만 건다. 정수는 단위를 글자로만 붙인다.
                const InspectorDisplayUnit unit      = InspectorPropertyLayout::getDisplayUnit( prop );
                const float64              scale     = kIsInteger ? 1.0 : static_cast<float64>( unit._scale );
                const float32              dragSpeed = kIsInteger ? 1.0f : ( unit._dragSpeed > 0.0f ? unit._dragSpeed : 0.01f );
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    if constexpr ( kIsInteger )
                        formatstring( buf.data(), buf.capacity(), "%#", *pPtr );
                    else
                        formatstring( buf.data(), buf.capacity(), "%#", static_cast<float64>( *pPtr ) * scale );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }

                // 위젯은 `UIMin` · `UIMax`(없으면 `Min` · `Max`) 안에서 움직이고, 값은 늘 `Min` · `Max` 로 막는다(`InspectorNumericRange`).
                // 적히지 않은 쪽은 nullptr 이라 ImGui 가 타입의 범위로 막는다(uint8 은 0..255).
                const InspectorNumericRange range    = InspectorPropertyLayout::getNumericRange( prop );
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
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        // ------------------------------------------------------------------------------
        // 그 밖의 내장 타입
        // ------------------------------------------------------------------------------
        /** @brief bool · atomic<bool> 체크박스입니다. 정수 비트필드와 같은 체크박스를 씁니다. */
        template <typename T>
        class CheckboxProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                if constexpr ( std::is_same_v<T, bool> )
                {
                    if ( prop._bIsBitField == SW_TRUE )
                        return drawBitFieldCheckbox( pInstance, prop );
                }

                T* pPtr = prop.getValuePtr<T>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    drawReadOnlyValue( prop, pPtr );
                    return true;
                }
                drawValueWidget( _pLabel, *pPtr );
                showTooltipIfHovered( prop );
                // atomic<bool> 은 다른 스레드가 쓰는 값이라 되돌리기 기록에 넣지 않는다.
                if constexpr ( std::is_same_v<T, bool> )
                    InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        /** @brief 메타데이터가 필요 없는 갈래(행렬 · 회전 · 핸들 · 태그)의 프로퍼티입니다. 고치는 위젯은 인자 칸과 같습니다. */
        template <typename T>
        class ValueProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                T* pPtr = prop.getValuePtr<T>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    drawReadOnlyValue( prop, pPtr );
                    return true;
                }
                drawValueWidget( _pLabel, *pPtr );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class StringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                const bool bAssetPath = prop._metadata._bAssetPath != SW_FALSE || prop._metadata._assetType.empty() == false;

                string* pPtr = prop.getValuePtr<string>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
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
                    // `FileFilter` 에 맞지 않는 파일은 받지 않는다. 드롭 · 고르기 · 지우기는 위젯 편집이 아니라 그리드가 입혀야 통지 · 되돌리기에 남는다.
                    if ( action == AssetFieldAction::Dropped && InspectorPropertyLayout::matchesFileFilter( prop._metadata._fileFilter, droppedPath ) )
                        grid.applyPropertyTextAsEdit( pInstance, prop, droppedPath, "Set Asset" );
                    else if ( action == AssetFieldAction::Cleared )
                        grid.applyPropertyTextAsEdit( pInstance, prop, "", "Clear Asset" );
                }
                else if ( prop._metadata._bMultiline != SW_FALSE )
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
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                const bool bAssetPath = prop._metadata._bAssetPath != SW_FALSE || prop._metadata._assetType.empty() == false;

                hashed_string* pPtr = prop.getValuePtr<hashed_string>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
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
                    if ( action == AssetFieldAction::Dropped && InspectorPropertyLayout::matchesFileFilter( prop._metadata._fileFilter, droppedPath ) )
                        grid.applyPropertyTextAsEdit( pInstance, prop, droppedPath, "Set Asset" );
                    else if ( action == AssetFieldAction::Cleared )
                        grid.applyPropertyTextAsEdit( pInstance, prop, "", "Clear Asset" );
                }
                else
                {
                    drawNameInput( _pLabel, *pPtr );
                }
                showTooltipIfHovered( prop );
                // 인턴 인덱스 하나라 POD 로 되돌린다 — 인턴된 문자열은 해제되지 않으므로 옛 인덱스는 언제나 유효하다.
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float3Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float3* pPtr = prop.getValuePtr<float3>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                // 라디안으로 저장한 각도(`Units=rad`)는 도로 보이고 고친다 — 바뀌었을 때만 되쓴다.
                const InspectorDisplayUnit unit = InspectorPropertyLayout::getDisplayUnit( prop );
                if ( prop._metadata._bReadOnly != SW_FALSE )
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
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float2Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float2* pPtr = prop.getValuePtr<float2>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer128> buf;
                    formatstring( buf.data(), buf.capacity(), "(%#, %#)",
                                  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                ImGui::DragFloat2( _pLabel, &pPtr->_x, 0.1f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        class Float4Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop, EditorPropertyGrid& /*grid*/ ) override
            {
                float4* pPtr = prop.getValuePtr<float4>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
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
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), prop );
                return true;
            }
        };

        /**
         * @brief 내장 타입 하나의 프로퍼티 위젯을 만듭니다. 갈래는 `InspectorWidgetFor<T>` 가 정합니다.
         * @details 메타데이터(범위 · 단위 · 색 · 애셋 경로)를 읽는 갈래는 전용 클래스, 나머지는 `ValueProperty` 입니다.
         */
        template <typename T>
        unique_ptr<IInspectorProperty> createBuiltinProperty()
        {
            constexpr InspectorValueWidget kWidget = InspectorWidgetFor<T>::kWidget;
            if constexpr ( kWidget == InspectorValueWidget::Number )
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
        using DrawerRegistry               = EditorRegistry<EditorPropertyDrawerRegistration>;
        const EditorRegistrationList& list = DrawerRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();
        // 줄이 빠졌으면 내장 위젯부터 다시 건다(등록 줄이 가렸던 것이 돌아온다). 그다음 지금 줄을 모두 건다.
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
            registerBuiltins(); // 가렸던 내장 위젯을 되살린다
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

    bool InspectorPropertyManager::drawMethodArg( const utf8* pLabel, InspectorMethodArgSlot& slot )
    {
        if ( slot._value.hasValue() == false || slot._builtinIndex >= std::size( kArrDrawMethodArg ) )
        {
            ImGui::TextDisabled( "%s (unsupported in UI)", pLabel );
            return false;
        }
        return kArrDrawMethodArg[slot._builtinIndex]( pLabel, slot._value );
    }
} // namespace sw::editor
