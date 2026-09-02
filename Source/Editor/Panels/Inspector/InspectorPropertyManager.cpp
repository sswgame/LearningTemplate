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
        class BuiltinPropertyBase : public IInspectorProperty
        {
        protected:
            const utf8* _pLabel = "##value";
            void        showTooltipIfHovered( const PropertyInfo& prop )
            {
                if ( prop._metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s", prop._metadata._tooltip.c_str() );
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
        };

        class Int32Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool    bReadOnly = prop._metadata._bReadOnly != 0;
                const bool    bHasRange = prop._metadata._bHasRange != 0;
                const float32 minF      = prop._metadata._minRange;
                const float32 maxF      = prop._metadata._maxRange;
                const int32   minI      = static_cast<int32>( minF );
                const int32   maxI      = static_cast<int32>( maxF );
                const string  fmt       = getFormatWithUnits( prop, "%d" );

                int32* pPtr = prop.getValuePtr<int32>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<int32>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                if ( bHasRange && isSliderRequested( prop ) )
                    ImGui::SliderInt( _pLabel, pPtr, minI, maxI, fmt.c_str() );
                else if ( bHasRange )
                    ImGui::DragInt( _pLabel, pPtr, 1.0f, minI, maxI, fmt.c_str() );
                else
                    ImGui::DragInt( _pLabel, pPtr, 1.0f, 0, 0, fmt.c_str() );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Uint32Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool    bReadOnly = prop._metadata._bReadOnly != 0;
                const bool    bHasRange = prop._metadata._bHasRange != 0;
                const float32 minF      = prop._metadata._minRange;
                const float32 maxF      = prop._metadata._maxRange;
                const int32   minI      = static_cast<int32>( minF );
                const int32   maxI      = static_cast<int32>( maxF );
                const string  fmt       = getFormatWithUnits( prop, "%u" );

                uint32* pPtr = prop.getValuePtr<uint32>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<uint32>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                int32 tmp = static_cast<int32>( *pPtr );
                if ( bHasRange && isSliderRequested( prop ) )
                {
                    if ( ImGui::SliderInt( _pLabel, &tmp, minI, maxI, fmt.c_str() ) )
                        *pPtr = static_cast<uint32>( tmp );
                }
                else if ( bHasRange )
                {
                    if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, minI, maxI, fmt.c_str() ) )
                        *pPtr = static_cast<uint32>( tmp );
                }
                else if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, 0, 0, fmt.c_str() ) )
                    *pPtr = static_cast<uint32>( tmp );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Int64Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool    bReadOnly = prop._metadata._bReadOnly != 0;
                const bool    bHasRange = prop._metadata._bHasRange != 0;
                const float32 minF      = prop._metadata._minRange;
                const float32 maxF      = prop._metadata._maxRange;
                const int32   minI      = static_cast<int32>( minF );
                const int32   maxI      = static_cast<int32>( maxF );
                const string  fmt       = getFormatWithUnits( prop, "%lld" );

                int64* pPtr = prop.getValuePtr<int64>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<int64>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                int32 tmp = static_cast<int32>( *pPtr );
                if ( bHasRange && isSliderRequested( prop ) )
                {
                    if ( ImGui::SliderInt( _pLabel, &tmp, minI, maxI, fmt.c_str() ) )
                        *pPtr = static_cast<int64>( tmp );
                }
                else if ( bHasRange )
                {
                    if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, minI, maxI, fmt.c_str() ) )
                        *pPtr = static_cast<int64>( tmp );
                }
                else if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, 0, 0, fmt.c_str() ) )
                    *pPtr = static_cast<int64>( tmp );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Float32Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool    bReadOnly = prop._metadata._bReadOnly != 0;
                const bool    bHasRange = prop._metadata._bHasRange != 0;
                const float32 minF      = prop._metadata._minRange;
                const float32 maxF      = prop._metadata._maxRange;
                const string  fmt       = getFormatWithUnits( prop, "%.2f" );

                float32* pPtr = prop.getValuePtr<float32>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<float64>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                if ( bHasRange && isSliderRequested( prop ) )
                    ImGui::SliderFloat( _pLabel, pPtr, minF, maxF, fmt.c_str() );
                else if ( bHasRange )
                    ImGui::DragFloat( _pLabel, pPtr, 0.01f, minF, maxF, fmt.c_str() );
                else
                    ImGui::DragFloat( _pLabel, pPtr, 0.01f, 0.0f, 0.0f, fmt.c_str() );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Float64Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool    bReadOnly = prop._metadata._bReadOnly != 0;
                const bool    bHasRange = prop._metadata._bHasRange != 0;
                const float32 minF      = prop._metadata._minRange;
                const float32 maxF      = prop._metadata._maxRange;
                const string  fmt       = getFormatWithUnits( prop, "%.2f" );

                float64* pPtr = prop.getValuePtr<float64>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<float64>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                float32 tmp = static_cast<float32>( *pPtr );
                if ( bHasRange && isSliderRequested( prop ) )
                {
                    if ( ImGui::SliderFloat( _pLabel, &tmp, minF, maxF, fmt.c_str() ) )
                        *pPtr = static_cast<float64>( tmp );
                }
                else if ( bHasRange )
                {
                    if ( ImGui::DragFloat( _pLabel, &tmp, 0.01f, minF, maxF, fmt.c_str() ) )
                        *pPtr = static_cast<float64>( tmp );
                }
                else if ( ImGui::DragFloat( _pLabel, &tmp, 0.01f, 0.0f, 0.0f, fmt.c_str() ) )
                    *pPtr = static_cast<float64>( tmp );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class Uint8Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool bReadOnly = prop._metadata._bReadOnly != 0;

                if ( prop._bIsBitField == SW_TRUE )
                {
                    bool bVal = prop.getValue<bool>( pInstance );
                    if ( bReadOnly )
                    {
                        drawReadOnlyText( prop, bVal ? "true" : "false" );
                        return true;
                    }
                    if ( ImGui::Checkbox( _pLabel, &bVal ) )
                    {
                        prop.setValue<bool>( pInstance, bVal );
                    }
                    showTooltipIfHovered( prop );
                    return true;
                }

                uint8* pPtr = prop.getValuePtr<uint8>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    fixed_string<constant::kMaxBuffer64> buf;
                    formatstring( buf.data(), buf.capacity(), "%#", static_cast<uint32>( *pPtr ) );
                    drawReadOnlyText( prop, buf.c_str() );
                    return true;
                }
                int32 tmp = static_cast<int32>( *pPtr );
                if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, 0, 255, "%u" ) )
                    *pPtr = static_cast<uint8>( tmp );
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackPod( pPtr, sizeof( *pPtr ), _pLabel );
                return true;
            }
        };

        class BoolProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool bReadOnly = prop._metadata._bReadOnly != 0;

                if ( prop._bIsBitField == SW_TRUE )
                {
                    bool bVal = prop.getValue<bool>( pInstance );
                    if ( bReadOnly )
                    {
                        drawReadOnlyText( prop, bVal ? "true" : "false" );
                        return true;
                    }
                    if ( ImGui::Checkbox( _pLabel, &bVal ) )
                    {
                        prop.setValue<bool>( pInstance, bVal );
                    }
                    showTooltipIfHovered( prop );
                    return true;
                }

                bool* pPtr = prop.getValuePtr<bool>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
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
                if ( prop._metadata._bReadOnly != 0 )
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
                const bool bReadOnly  = prop._metadata._bReadOnly != 0;
                const bool bAssetPath = prop._metadata._bAssetPath != 0 || prop._metadata._assetType.empty() == false;

                string* pPtr = prop.getValuePtr<string>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    drawReadOnlyText( prop, pPtr->c_str() );
                    return true;
                }

                if ( bAssetPath )
                {
                    ImGui::PushID( _pLabel );
                    const float32 buttonWidth = 24.0f;
                    const float32 itemWidth   = ImGui::CalcItemWidth();
                    ImGui::SetNextItemWidth( ( itemWidth > buttonWidth + 10.0f ) ? itemWidth - buttonWidth - 4.0f : itemWidth );

                    fixed_string<constant::kMaxBuffer512> buf{ pPtr->c_str() };
                    if ( ImGui::InputText( "##assetInput", buf.data(), buf.capacity() ) )
                        *pPtr = buf.c_str();

                    if ( ImGui::BeginDragDropTarget() )
                    {
                        string droppedPath;
                        if ( EditorWidgets::tryAcceptAssetPayload( droppedPath ) )
                            *pPtr = droppedPath;
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if ( ImGui::Button( "x##clear", ImVec2( buttonWidth, 0 ) ) )
                        pPtr->clear();
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Clear asset reference" );

                    ImGui::PopID();
                    showTooltipIfHovered( prop );
                    InspectorPropertyUndo::trackString( pPtr, _pLabel );
                    return true;
                }

                fixed_string<constant::kMaxBuffer512> buf{ pPtr->c_str() };
                if ( ImGui::InputText( _pLabel, buf.data(), buf.capacity() ) )
                    *pPtr = buf.c_str();
                showTooltipIfHovered( prop );
                InspectorPropertyUndo::trackString( pPtr, _pLabel );
                return true;
            }
        };

        class Float3Property : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool bReadOnly = prop._metadata._bReadOnly != 0;

                float3* pPtr = prop.getValuePtr<float3>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
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
                const bool bReadOnly = prop._metadata._bReadOnly != 0;

                float2* pPtr = prop.getValuePtr<float2>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
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
                const bool bReadOnly = prop._metadata._bReadOnly != 0;

                float4* pPtr = prop.getValuePtr<float4>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
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

        class HashedStringProperty : public BuiltinPropertyBase
        {
        public:
            bool draw( void* pInstance, const PropertyInfo& prop ) override
            {
                const bool bReadOnly  = prop._metadata._bReadOnly != 0;
                const bool bAssetPath = prop._metadata._bAssetPath != 0 || prop._metadata._assetType.empty() == false;

                hashed_string* pPtr = prop.getValuePtr<hashed_string>( pInstance );
                if ( pPtr == nullptr )
                    return true;
                if ( bReadOnly )
                {
                    drawReadOnlyText( prop, pPtr->c_str() );
                    return true;
                }

                if ( bAssetPath )
                {
                    ImGui::PushID( _pLabel );
                    const float32 buttonWidth = 24.0f;
                    const float32 itemWidth   = ImGui::CalcItemWidth();
                    ImGui::SetNextItemWidth( ( itemWidth > buttonWidth + 10.0f ) ? itemWidth - buttonWidth - 4.0f : itemWidth );

                    fixed_string<constant::kMaxBuffer256> buf{ pPtr->c_str() };
                    if ( ImGui::InputText( "##assetInput", buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) )
                        *pPtr = hashed_string( buf.c_str() );

                    if ( ImGui::BeginDragDropTarget() )
                    {
                        string droppedPath;
                        if ( EditorWidgets::tryAcceptAssetPayload( droppedPath ) )
                            *pPtr = hashed_string( droppedPath.c_str() );
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if ( ImGui::Button( "x##clear", ImVec2( buttonWidth, 0 ) ) )
                        *pPtr = hashed_string{};
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Clear asset reference" );

                    ImGui::PopID();
                    showTooltipIfHovered( prop );
                    return true;
                }

                fixed_string<constant::kMaxBuffer256> buf{ pPtr->c_str() };
                if ( ImGui::InputText( _pLabel, buf.data(), buf.capacity(), ImGuiInputTextFlags_EnterReturnsTrue ) )
                    *pPtr = hashed_string( buf.c_str() );
                showTooltipIfHovered( prop );
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
        registerType( "int32", make_unique<Int32Property>() );
        registerType( "uint32", make_unique<Uint32Property>() );
        registerType( "uint8", make_unique<Uint8Property>() );
        registerType( "int64", make_unique<Int64Property>() );
        registerType( "float32", make_unique<Float32Property>() );
        registerType( "float64", make_unique<Float64Property>() );
        registerType( "bool", make_unique<BoolProperty>() );
        registerType( "atomic_bool", make_unique<AtomicBoolProperty>() );
        registerType( "string", make_unique<StringProperty>() );
        registerType( "float3", make_unique<Float3Property>() );
        registerType( "float2", make_unique<Float2Property>() );
        registerType( "float4", make_unique<Float4Property>() );
        registerType( "hashed_string", make_unique<HashedStringProperty>() );
    }
} // namespace sw::editor
