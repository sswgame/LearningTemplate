#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyManager.h"

#include "Core/Concurrency/atomic.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 애셋 경로 필드에서 이번 프레임에 일어난 일. */
        enum class AssetFieldAction : uint8
        {
            None,
            Dropped, ///< 드래그 드롭으로 경로가 들어왔다
            Cleared, ///< 지우기 버튼
        };

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
            string getFormatWithUnits( const PropertyInfo& prop, const utf8* pDefaultFmt )
            {
                const string* pUnits = prop.findCustomMeta( hashed_string( "Units" ) );
                if ( pUnits != nullptr && pUnits->empty() == false )
                {
                    string fmt = pDefaultFmt;
                    fmt += " ";
                    fmt += *pUnits;
                    return fmt;
                }
                return string{ pDefaultFmt };
            }
            bool isSliderRequested( const PropertyInfo& prop )
            {
                return prop.findCustomMeta( hashed_string( "Slider" ) ) != nullptr;
            }
            bool isColorRequested( const PropertyInfo& prop )
            {
                if ( prop.findCustomMeta( hashed_string( "Color" ) ) != nullptr )
                    return true;
                if ( StringUtil::stristr( prop._name.c_str(), "color" ) != nullptr )
                    return true;
                return false;
            }

            /** @brief 비트필드는 포인터를 못 잡으므로 값으로 읽고 쓴다. 정수 계열과 bool 이 같은 체크박스다. */
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
             * @brief 애셋 경로 필드의 틀 — 입력 칸 · 드롭 대상 · 지우기 버튼.
             * @details 입력 칸만 호출자가 그린다: `string` 은 키 입력마다 반영하고, `hashed_string` 은 Enter 로 확정한다
             *          (키 입력마다 인턴하면 그 문자열이 전부 표에 남는다). 드롭과 지우기의 처리도 호출자 몫이다.
             */
            template <typename DrawInputFn>
            AssetFieldAction drawAssetPathField( DrawInputFn&& drawInput, string& outDroppedPath )
            {
                constexpr float32 kButtonWidth = 24.0f;
                AssetFieldAction  action       = AssetFieldAction::None;

                ImGui::PushID( _pLabel );
                const float32 itemWidth = ImGui::CalcItemWidth();
                ImGui::SetNextItemWidth( ( itemWidth > kButtonWidth + 10.0f ) ? itemWidth - kButtonWidth - 4.0f : itemWidth );

                drawInput( "##assetInput" );

                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( EditorWidgets::tryAcceptAssetPayload( outDroppedPath ) )
                        action = AssetFieldAction::Dropped;
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if ( ImGui::Button( "x##clear", ImVec2( kButtonWidth, 0 ) ) )
                    action = AssetFieldAction::Cleared;
                EditorWidgets::drawTooltip( "Clear asset reference" );

                ImGui::PopID();
                return action;
            }
        };

        // ------------------------------------------------------------------------------
        // 숫자 프로퍼티 — 타입마다 다른 것은 표 한 줄이고, 그리는 길은 하나다
        // ------------------------------------------------------------------------------
        /**
         * @brief 숫자 타입 하나의 표 한 줄: 위젯이 다루는 값 타입(ImGui 는 int32 · float32 만 안다) · 읽기 전용 표시용 타입 ·
         *        서식 · 드래그 속도 · 범위 메타가 없을 때의 한계(0·0 은 무제한).
         * @details 예전에는 int32 · uint32 · int64 · uint8 · float32 · float64 가 각자 40 줄짜리 `draw` 를 들었고 —
         *          같은 여섯 걸음(포인터 · 읽기 전용 · 범위 · 슬라이더 · 툴팁 · 되돌리기)에 캐스트만 달랐다. 폭이 다른 타입은
         *          임시값으로 위젯을 오가고, 바뀌었을 때만 되쓴다.
         */
        template <typename T>
        struct NumericPropertyTraits;

        template <>
        struct NumericPropertyTraits<int32>
        {
            using Widget                            = int32;
            using Print                             = int32;
            static constexpr const utf8* kFormat    = "%d";
            static constexpr float32     kDragSpeed = 1.0f;
            static constexpr int32       kMin       = 0;
            static constexpr int32       kMax       = 0;
            static constexpr bool        kIsInteger = true;
        };

        template <>
        struct NumericPropertyTraits<uint32>
        {
            using Widget                            = int32;
            using Print                             = uint32;
            static constexpr const utf8* kFormat    = "%u";
            static constexpr float32     kDragSpeed = 1.0f;
            static constexpr int32       kMin       = 0;
            static constexpr int32       kMax       = 0;
            static constexpr bool        kIsInteger = true;
        };

        template <>
        struct NumericPropertyTraits<int64>
        {
            using Widget                            = int32;
            using Print                             = int64;
            static constexpr const utf8* kFormat    = "%d";
            static constexpr float32     kDragSpeed = 1.0f;
            static constexpr int32       kMin       = 0;
            static constexpr int32       kMax       = 0;
            static constexpr bool        kIsInteger = true;
        };

        template <>
        struct NumericPropertyTraits<uint8>
        {
            using Widget                            = int32;
            using Print                             = uint32;
            static constexpr const utf8* kFormat    = "%u";
            static constexpr float32     kDragSpeed = 1.0f;
            static constexpr int32       kMin       = 0;
            static constexpr int32       kMax       = 255;
            static constexpr bool        kIsInteger = true;
        };

        template <>
        struct NumericPropertyTraits<float32>
        {
            using Widget                            = float32;
            using Print                             = float64;
            static constexpr const utf8* kFormat    = "%.2f";
            static constexpr float32     kDragSpeed = 0.01f;
            static constexpr float32     kMin       = 0.0f;
            static constexpr float32     kMax       = 0.0f;
            static constexpr bool        kIsInteger = false;
        };

        template <>
        struct NumericPropertyTraits<float64>
        {
            using Widget                            = float32;
            using Print                             = float64;
            static constexpr const utf8* kFormat    = "%.2f";
            static constexpr float32     kDragSpeed = 0.01f;
            static constexpr float32     kMin       = 0.0f;
            static constexpr float32     kMax       = 0.0f;
            static constexpr bool        kIsInteger = false;
        };

        /** @brief 범위가 있고 슬라이더를 청했으면 슬라이더, 아니면 드래그. 0·0 범위는 무제한이다. */
        bool drawNumberWidget( const utf8* pLabel, int32* pValue, float32 dragSpeed, int32 minValue, int32 maxValue,
                               const utf8* pFormat, bool bSlider )
        {
            if ( bSlider )
                return ImGui::SliderInt( pLabel, pValue, minValue, maxValue, pFormat );
            return ImGui::DragInt( pLabel, pValue, dragSpeed, minValue, maxValue, pFormat );
        }

        bool drawNumberWidget( const utf8* pLabel, float32* pValue, float32 dragSpeed, float32 minValue, float32 maxValue,
                               const utf8* pFormat, bool bSlider )
        {
            if ( bSlider )
                return ImGui::SliderFloat( pLabel, pValue, minValue, maxValue, pFormat );
            return ImGui::DragFloat( pLabel, pValue, dragSpeed, minValue, maxValue, pFormat );
        }

        template <typename T>
        class NumericProperty : public BuiltinPropertyBase
        {
            using Traits = NumericPropertyTraits<T>;
            using Widget = typename Traits::Widget;

        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                if constexpr ( Traits::kIsInteger )
                {
                    if ( prop._bIsBitField == SW_TRUE )
                        return drawBitFieldCheckbox( pInstance, prop );
                }

                T* pPtr = prop.getValuePtr<T>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<typename Traits::Print>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }

                const bool   bHasRange = prop._metadata._bHasRange != SW_FALSE;
                const Widget minValue  = bHasRange ? static_cast<Widget>( prop._metadata._minRange ) : Traits::kMin;
                const Widget maxValue  = bHasRange ? static_cast<Widget>( prop._metadata._maxRange ) : Traits::kMax;
                const bool   bSlider   = bHasRange && isSliderRequested( prop );
                const string fmt       = getFormatWithUnits( prop, Traits::kFormat );

                Widget widgetValue = static_cast<Widget>( *pPtr );
                if ( drawNumberWidget( _pLabel, &widgetValue, Traits::kDragSpeed, minValue, maxValue, fmt.c_str(), bSlider ) )
                    *pPtr = static_cast<T>( widgetValue );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        // ------------------------------------------------------------------------------
        // 그 밖의 내장 타입
        // ------------------------------------------------------------------------------
        class BoolProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                if ( prop._bIsBitField == SW_TRUE )
                    return drawBitFieldCheckbox( pInstance, prop );

                bool* pPtr = prop.getValuePtr<bool>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, *pPtr ? "true" : "false" );
                    return true;
                }
                ImGui::Checkbox( _pLabel, pPtr );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class AtomicBoolProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                atomic<bool>* pPtr = prop.getValuePtr<atomic<bool>>( pInstance );
                if ( pPtr == nullptr )
                    return true;

                bool value = pPtr->load( std::memory_order_relaxed );
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    drawReadOnlyText( prop, value ? "true" : "false" );
                    return true;
                }
                if ( ImGui::Checkbox( _pLabel, &value ) )
                    pPtr->store( value, std::memory_order_relaxed );
                showTooltipIfHovered( prop );
                return true;
            }
        };

        class StringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
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
                        [pPtr]( const utf8* pId )
                    { EditorWidgets::drawTextField( pId, *pPtr ); }, droppedPath );
                    if ( action == AssetFieldAction::Dropped )
                        *pPtr = droppedPath;
                    else if ( action == AssetFieldAction::Cleared )
                        pPtr->clear();
                }
                else
                {
                    EditorWidgets::drawTextField( _pLabel, *pPtr );
                }
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackString( pPtr, _pLabel );
                return true;
            }
        };

        class HashedStringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
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
                        [pPtr]( const utf8* pId )
                    { drawHashedStringInput( pId, *pPtr ); }, droppedPath );
                    if ( action == AssetFieldAction::Dropped )
                        *pPtr = hashed_string( droppedPath.c_str() );
                    else if ( action == AssetFieldAction::Cleared )
                        *pPtr = hashed_string{};
                }
                else
                {
                    drawHashedStringInput( _pLabel, *pPtr );
                }
                showTooltipIfHovered( prop );
                // 인턴 인덱스 하나라 POD 로 되돌린다 — 인턴된 문자열은 해제되지 않으므로 옛 인덱스는 언제나 유효하다.
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }

        private:
            /** @brief Enter 로 확정하는 입력 칸. 키 입력마다 인턴하지 않으려는 것이다. */
            static void drawHashedStringInput( const utf8* pId, hashed_string& value )
            {
                fixed_string<constant::kMaxBuffer256> buf{ value.c_str() };
                if ( ImGui::InputText( pId, buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) )
                    value = hashed_string( buf.c_str() );
            }
        };

        class Float3Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                float3* pPtr = prop.getValuePtr<float3>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( prop._metadata._bReadOnly != SW_FALSE )
                {
                    fixed_string<constant::kMaxBuffer128> buf;
                    formatstring( buf.data(), buf.capacity(), "(%#, %#)",
                                  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ), Fmt( pPtr->_z, Format( 2 ) ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                if ( isColorRequested( prop ) )
                    ImGui::ColorEdit3( _pLabel, &pPtr->_x, ImGuiColorEditFlags_Float );
                else
                    EditorWidgets::drawVec3Control( _pLabel, *pPtr, 0.0f, 100.0f, 0.1f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Float2Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
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
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Float4Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
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
                    ImGui::ColorEdit4( _pLabel, &pPtr->_x, ImGuiColorEditFlags_Float );
                else
                    ImGui::DragFloat4( _pLabel, &pPtr->_x, 0.01f );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };
    } // namespace

    void InspectorPropertyManager::registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty )
    {
        _mapProperty[string{ typeName }] = std::move( pProperty );
    }

    IInspectorProperty* InspectorPropertyManager::find( string_view typeName ) const
    {
        const auto it = _mapProperty.find( string{ typeName } );
        if ( it != _mapProperty.end() )
            return it->second.get();
        return nullptr;
    }

    void InspectorPropertyManager::registerDefaults()
    {
        registerType( "int32", make_unique<NumericProperty<int32>>() );
        registerType( "uint32", make_unique<NumericProperty<uint32>>() );
        registerType( "uint8", make_unique<NumericProperty<uint8>>() );
        registerType( "int64", make_unique<NumericProperty<int64>>() );
        registerType( "float32", make_unique<NumericProperty<float32>>() );
        registerType( "float64", make_unique<NumericProperty<float64>>() );
        registerType( "bool", make_unique<BoolProperty>() );
        registerType( "atomic_bool", make_unique<AtomicBoolProperty>() );
        registerType( "string", make_unique<StringProperty>() );
        registerType( "float3", make_unique<Float3Property>() );
        registerType( "float2", make_unique<Float2Property>() );
        registerType( "float4", make_unique<Float4Property>() );
        registerType( "hashed_string", make_unique<HashedStringProperty>() );
    }
} // namespace sw::editor
