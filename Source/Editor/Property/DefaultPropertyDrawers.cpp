#include "pch.h"

#include "Editor/Property/DefaultPropertyDrawers.h"
#include "Editor/Property/IPropertyDrawer.h"
#include "Editor/Property/PropertyDrawerHelper.h"
#include "Editor/Property/PropertyDrawerRegistry.h"
#include "Editor/Widgets/EditorWidgets.h"
#include "Engine/Utility/CommandStack.h"
#include "Editor/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/EditorService.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace sw
{
	void trackPodPropertyUndo( void* pData, size_t size, const utf8* pLabel )
	{
		if ( pData == nullptr || size == 0 || size > 512 )
			return;

		struct Entry
		{
			vector<uint8> _listBefore;
			void*		  _pPtr{ nullptr };
			size_t		  _size{ 0 };
			string		  _label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;

		const ImGuiID id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pPtr	 = pData;
			e._size	 = size;
			e._label = ( pLabel != nullptr ) ? pLabel : "Property";
			e._listBefore.assign( static_cast<const uint8*>( pData ), static_cast<const uint8*>( pData ) + size );
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;

		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;

		vector<uint8> after( static_cast<const uint8*>( pData ), static_cast<const uint8*>( pData ) + size );
		if ( after == it->second._listBefore )
		{
			s_mapPending.erase( it );
			return;
		}

		void*		  pPtr		 = it->second._pPtr;
		const size_t  sz		 = it->second._size;
		vector<uint8> listBefore = std::move( it->second._listBefore );
		const string  lbl		 = string( "Edit " ) + it->second._label;
		s_mapPending.erase( it );

		uint64						selectedId = EditorWorkspace::selectedObjectId();
		CommandStack::Command cmd;
		cmd._label = lbl;
		cmd._undo  = [pPtr, sz, listBefore, selectedId]()
		{
			if ( pPtr != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				Memory::copy( pPtr, listBefore.data(), sz );
		};
		cmd._redo = [pPtr, sz, after, selectedId]()
		{
			if ( pPtr != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				Memory::copy( pPtr, after.data(), sz );
		};
		editor::getService<CommandStack>()->push( std::move( cmd ) );
	}

	void trackStringPropertyUndo( string* pPtr, const utf8* pLabel )
	{
		if ( pPtr == nullptr )
			return;
		struct Entry
		{
			string	_before;
			string* _pPtr{ nullptr };
			string	_label;
		};
		static unordered_map<ImGuiID, Entry> s_mapPending;
		const ImGuiID						 id = ImGui::GetItemID();
		if ( ImGui::IsItemActivated() )
		{
			Entry e;
			e._pPtr			 = pPtr;
			e._before		 = *pPtr;
			e._label		 = ( pLabel != nullptr ) ? pLabel : "Property";
			s_mapPending[id] = std::move( e );
		}
		if ( ImGui::IsItemDeactivatedAfterEdit() == false )
			return;
		const auto it = s_mapPending.find( id );
		if ( it == s_mapPending.end() )
			return;
		string after = *pPtr;
		if ( after == it->second._before )
		{
			s_mapPending.erase( it );
			return;
		}
		string*		 pTarget = it->second._pPtr;
		string		 before	 = std::move( it->second._before );
		const string lbl	 = string( "Edit " ) + it->second._label;
		s_mapPending.erase( it );

		uint64						selectedId = EditorWorkspace::selectedObjectId();
		CommandStack::Command cmd;
		cmd._label = lbl;
		cmd._undo  = [pTarget, before, selectedId]()
		{
			if ( pTarget != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				*pTarget = before;
		};
		cmd._redo = [pTarget, after, selectedId]()
		{
			if ( pTarget != nullptr && ( selectedId == 0 || EditorWorkspace::selectedObjectId() == selectedId ) )
				*pTarget = after;
		};
		editor::getService<CommandStack>()->push( std::move( cmd ) );
	}

	namespace
	{
		class BuiltinDrawerBase : public IPropertyDrawer
		{
		protected:
			const utf8* pLabel = "##value";
			void		showTooltipIfHovered( const PropertyInfo& prop )
			{
				if ( prop._metadata._tooltip.empty() == false && ImGui::IsItemHovered() )
					ImGui::SetTooltip( "%s", prop._metadata._tooltip.c_str() );
			}
			void drawReadOnlyText( const PropertyInfo& prop, const utf8* pValue )
			{
				ImGui::TextDisabled( "%s", pLabel );
				ImGui::SameLine();
				ImGui::TextUnformatted( pValue != nullptr ? pValue : "" );
				showTooltipIfHovered( prop );
			}
		};
		class Int32Drawer : public BuiltinDrawerBase
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
					ImGui::DragInt( pLabel, pPtr, 1.0f, minI, maxI );
				else
					ImGui::DragInt( pLabel, pPtr );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Uint32Drawer : public BuiltinDrawerBase
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
					if ( ImGui::DragInt( pLabel, &tmp, 1.0f, minI, maxI ) )
						*pPtr = static_cast<uint32>( tmp );
				}
				else if ( ImGui::DragInt( pLabel, &tmp, 1.0f, 0 ) )
					*pPtr = static_cast<uint32>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Int64Drawer : public BuiltinDrawerBase
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
					if ( ImGui::DragInt( pLabel, &tmp, 1.0f, minI, maxI ) )
						*pPtr = static_cast<int64>( tmp );
				}
				else if ( ImGui::DragInt( pLabel, &tmp ) )
					*pPtr = static_cast<int64>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Float32Drawer : public BuiltinDrawerBase
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
					ImGui::DragFloat( pLabel, pPtr, 0.01f, minF, maxF );
				else
					ImGui::DragFloat( pLabel, pPtr, 0.01f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Float64Drawer : public BuiltinDrawerBase
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
					if ( ImGui::DragFloat( pLabel, &tmp, 0.01f, minF, maxF ) )
						*pPtr = static_cast<float64>( tmp );
				}
				else if ( ImGui::DragFloat( pLabel, &tmp, 0.01f ) )
					*pPtr = static_cast<float64>( tmp );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class BoolDrawer : public BuiltinDrawerBase
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
				ImGui::Checkbox( pLabel, pPtr );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class StringDrawer : public BuiltinDrawerBase
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
				if ( ImGui::InputText( pLabel, buf, sizeof( buf ) ) )
					*pPtr = buf;
				trackStringPropertyUndo( pPtr, pLabel );
				return true;
			}
		};

		class Float3Drawer : public BuiltinDrawerBase
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
				editor::drawVec3Control( pLabel, *pPtr, 0.0f, 100.0f, 0.1f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Float2Drawer : public BuiltinDrawerBase
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
				ImGui::DragFloat2( pLabel, &pPtr->_x, 0.1f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Float4Drawer : public BuiltinDrawerBase
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
				ImGui::DragFloat4( pLabel, &pPtr->_x, 0.01f );
				trackPodPropertyUndo( pPtr, sizeof( *pPtr ), pLabel );
				return true;
			}
		};

		class Hashed_stringDrawer : public BuiltinDrawerBase
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
				if ( ImGui::InputText( pLabel, buf, sizeof( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
					*pPtr = hashed_string( buf );
				return true;
			}
		};

	} // namespace

	void registerDefaultPropertyDrawers()
	{
		PropertyDrawerRegistry::registerDrawer( "int32", make_unique<Int32Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "uint32", make_unique<Uint32Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "int64", make_unique<Int64Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "float32", make_unique<Float32Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "float64", make_unique<Float64Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "bool", make_unique<BoolDrawer>() );
		PropertyDrawerRegistry::registerDrawer( "string", make_unique<StringDrawer>() );
		PropertyDrawerRegistry::registerDrawer( "float3", make_unique<Float3Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "float2", make_unique<Float2Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "float4", make_unique<Float4Drawer>() );
		PropertyDrawerRegistry::registerDrawer( "hashed_string", make_unique<Hashed_stringDrawer>() );
	}
} // namespace sw
