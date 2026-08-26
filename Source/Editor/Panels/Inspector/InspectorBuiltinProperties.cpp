#include "pch.h"

#include "Editor/Panels/Inspector/InspectorBuiltin.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorPropertyRegistry.h"
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
			void		showTooltipIfHovered( const PropertyInfo& prop )
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
		};
		class Int32Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				int32* pPtr = prop.getValuePtr<int32>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer64];
					formatstring( buf, sizeof( buf ), "%#", static_cast<int32>( *pPtr ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				if ( bHasRange )
					ImGui::DragInt( _pLabel, pPtr, 1.0f, minI, maxI );
				else
					ImGui::DragInt( _pLabel, pPtr );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Uint32Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				uint32* pPtr = prop.getValuePtr<uint32>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer64];
					formatstring( buf, sizeof( buf ), "%#", static_cast<uint32>( *pPtr ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				int32 tmp = static_cast<int32>( *pPtr );
				if ( bHasRange )
				{
					if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, minI, maxI ) )
						*pPtr = static_cast<uint32>( tmp );
				}
				else if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, 0 ) )
					*pPtr = static_cast<uint32>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Int64Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				int64* pPtr = prop.getValuePtr<int64>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer64];
					formatstring( buf, sizeof( buf ), "%#", static_cast<int64>( *pPtr ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				int32 tmp = static_cast<int32>( *pPtr );
				if ( bHasRange )
				{
					if ( ImGui::DragInt( _pLabel, &tmp, 1.0f, minI, maxI ) )
						*pPtr = static_cast<int64>( tmp );
				}
				else if ( ImGui::DragInt( _pLabel, &tmp ) )
					*pPtr = static_cast<int64>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Float32Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				float32* pPtr = prop.getValuePtr<float32>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer64];
					formatstring( buf, sizeof( buf ), "%#", static_cast<float64>( *pPtr ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				if ( bHasRange )
					ImGui::DragFloat( _pLabel, pPtr, 0.01f, minF, maxF );
				else
					ImGui::DragFloat( _pLabel, pPtr, 0.01f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Float64Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				float64* pPtr = prop.getValuePtr<float64>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer64];
					formatstring( buf, sizeof( buf ), "%#", static_cast<float64>( *pPtr ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				float32 tmp = static_cast<float32>( *pPtr );
				if ( bHasRange )
				{
					if ( ImGui::DragFloat( _pLabel, &tmp, 0.01f, minF, maxF ) )
						*pPtr = static_cast<float64>( tmp );
				}
				else if ( ImGui::DragFloat( _pLabel, &tmp, 0.01f ) )
					*pPtr = static_cast<float64>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class BoolProperty : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				bool* pPtr = prop.getValuePtr<bool>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					drawReadOnlyText( prop, *pPtr ? "true" : "false" );
					return true;
				}
				ImGui::Checkbox( _pLabel, pPtr );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class StringProperty : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				string* pPtr = prop.getValuePtr<string>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					drawReadOnlyText( prop, pPtr->c_str() );
					return true;
				}
				utf8 buf[constant::kMaxBuffer512];
				formatstring( buf, sizeof( buf ), "%#", pPtr->c_str() );
				if ( ImGui::InputText( _pLabel, buf, sizeof( buf ) ) )
					*pPtr = buf;
				trackStringPropertyUndo( pPtr, _pLabel );
				return true;
			}
		};

		class Float3Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				float3* pPtr = prop.getValuePtr<float3>( pInstance );
				// 쓰기 가능하면 RGB 축 컨트롤을 우선합니다.
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer128];
					formatstring( buf, sizeof( buf ), "(%#, %#)",
								  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ), Fmt( pPtr->_z, Format( 2 ) ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				editor::drawVec3Control( _pLabel, *pPtr, 0.0f, 100.0f, 0.1f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Float2Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				float2* pPtr = prop.getValuePtr<float2>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer128];
					formatstring( buf, sizeof( buf ), "(%#, %#)",
								  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				ImGui::DragFloat2( _pLabel, &pPtr->_x, 0.1f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class Float4Property : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				float4* pPtr = prop.getValuePtr<float4>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					utf8 buf[constant::kMaxBuffer256];
					formatstring( buf, sizeof( buf ), "(%#, %#, %#, %#)",
								  Fmt( pPtr->_x, Format( 2 ) ), Fmt( pPtr->_y, Format( 2 ) ),
								  Fmt( pPtr->_z, Format( 2 ) ), Fmt( pPtr->_w, Format( 2 ) ) );
					drawReadOnlyText( prop, buf );
					return true;
				}
				ImGui::DragFloat4( _pLabel, &pPtr->_x, 0.01f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), _pLabel );
				return true;
			}
		};

		class HashedStringProperty : public BuiltinPropertyBase
		{
		public:
			bool draw( void* pInstance, const PropertyInfo& prop ) override
			{
				const bool					 bReadOnly = prop._metadata._bReadOnly != 0;
				[[maybe_unused]] const bool	 bHasRange = prop._metadata._bHasRange != 0;
				const float32				 minF	   = prop._metadata._minRange;
				const float32				 maxF	   = prop._metadata._maxRange;
				[[maybe_unused]] const int32 minI	   = static_cast<int32>( minF );
				[[maybe_unused]] const int32 maxI	   = static_cast<int32>( maxF );

				hashed_string* pPtr = prop.getValuePtr<hashed_string>( pInstance );
				if ( pPtr == nullptr )
					return true;
				if ( bReadOnly )
				{
					drawReadOnlyText( prop, pPtr->c_str() );
					return true;
				}
				utf8 buf[constant::kMaxBuffer256];
				formatstring( buf, sizeof( buf ), "%#", pPtr->c_str() );
				if ( ImGui::InputText( _pLabel, buf, sizeof( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
					*pPtr = hashed_string( buf );
				return true;
			}
		};

	} // namespace

	void registerInspectorBuiltinProperties()
	{
		InspectorPropertyRegistry::registerType( "int32", make_unique<Int32Property>() );
		InspectorPropertyRegistry::registerType( "uint32", make_unique<Uint32Property>() );
		InspectorPropertyRegistry::registerType( "int64", make_unique<Int64Property>() );
		InspectorPropertyRegistry::registerType( "float32", make_unique<Float32Property>() );
		InspectorPropertyRegistry::registerType( "float64", make_unique<Float64Property>() );
		InspectorPropertyRegistry::registerType( "bool", make_unique<BoolProperty>() );
		InspectorPropertyRegistry::registerType( "string", make_unique<StringProperty>() );
		InspectorPropertyRegistry::registerType( "float3", make_unique<Float3Property>() );
		InspectorPropertyRegistry::registerType( "float2", make_unique<Float2Property>() );
		InspectorPropertyRegistry::registerType( "float4", make_unique<Float4Property>() );
		InspectorPropertyRegistry::registerType( "hashed_string", make_unique<HashedStringProperty>() );
	}
} // namespace sw::editor
