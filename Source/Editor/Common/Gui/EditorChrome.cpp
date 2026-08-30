#include "pch.h"

#include "Editor/Common/Gui/EditorChrome.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct EditorChromeInternal
		{
			static constexpr int32 kMaxSectionDepth = 8;
			static constexpr int32 kMaxOverlayDepth = 8;

			static ImGuiWindowFlags toImGuiPanelFlags( EditorPanelFlags flags )
			{
				ImGuiWindowFlags imguiFlags = 0;
				if ( ( flags & EditorPanelFlags::NoCollapse ) != EditorPanelFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoCollapse;
				if ( ( flags & EditorPanelFlags::MenuBar ) != EditorPanelFlags::None )
					imguiFlags |= ImGuiWindowFlags_MenuBar;
				if ( ( flags & EditorPanelFlags::NoScrollbar ) != EditorPanelFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoScrollbar;
				if ( ( flags & EditorPanelFlags::UnsavedDocument ) != EditorPanelFlags::None )
					imguiFlags |= ImGuiWindowFlags_UnsavedDocument;
				return imguiFlags;
			}

			static ImGuiWindowFlags toImGuiOverlayFlags( EditorOverlayFlags flags )
			{
				ImGuiWindowFlags imguiFlags = 0;
				if ( ( flags & EditorOverlayFlags::NoTitleBar ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoTitleBar;
				if ( ( flags & EditorOverlayFlags::NoResize ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoResize;
				if ( ( flags & EditorOverlayFlags::NoMove ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoMove;
				if ( ( flags & EditorOverlayFlags::NoInputs ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoInputs;
				if ( ( flags & EditorOverlayFlags::NoNav ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoNav;
				if ( ( flags & EditorOverlayFlags::AutoResize ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_AlwaysAutoResize;
				if ( ( flags & EditorOverlayFlags::NoFocusOnAppearing ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
				if ( ( flags & EditorOverlayFlags::NoSavedSettings ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoSavedSettings;
				if ( ( flags & EditorOverlayFlags::NoDecoration ) != EditorOverlayFlags::None )
					imguiFlags |= ImGuiWindowFlags_NoDecoration;
				return imguiFlags;
			}

			static inline thread_local EditorSectionKind s_arrSectionStack[kMaxSectionDepth]{};
			static inline thread_local int32			 s_sectionDepth{ 0 };
			static inline thread_local int32			 s_floatingBarDisabledDepth{ 0 };
			static inline thread_local int32			 s_arrOverlayStyleVars[kMaxOverlayDepth]{};
			static inline thread_local int32			 s_overlayDepth{ 0 };
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	EditorFloatingBarDesc::EditorFloatingBarDesc()
		: _pId{ "##FloatingBar" }
		, _anchorPos{ 0.0f, 0.0f }
		, _pivot{ 0.5f, 0.0f }
		, _flags{ EditorFloatingBarFlags::AutoResize | EditorFloatingBarFlags::NoMove |
				  EditorFloatingBarFlags::PassThroughWhenDisabled }
		, _bEnabled{ true }
	{
	}

	bool EditorChrome::beginPanel( const utf8* pTitle, bool* pOpen, EditorPanelFlags flags )
	{
		if ( pTitle == nullptr )
			return false;

		const bool bNoPadding = ( flags & EditorPanelFlags::NoPadding ) != EditorPanelFlags::None;
		if ( bNoPadding )
			ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f } );

		const bool bVisible = ImGui::Begin( pTitle, pOpen, EditorChromeInternal::toImGuiPanelFlags( flags ) );
		if ( bNoPadding )
			ImGui::PopStyleVar();
		return bVisible;
	}

	void EditorChrome::endPanel()
	{
		ImGui::End();
	}

	void EditorChrome::setNextPanelSize( const float2& size )
	{
		if ( size._x <= 0.0f || size._y <= 0.0f )
			return;
		ImGui::SetNextWindowSize( ImVec2{ size._x, size._y }, ImGuiCond_FirstUseEver );
	}

	bool EditorChrome::tryGetMainViewportRect( float2& outPos, float2& outSize )
	{
		const ImGuiViewport* pViewport = ImGui::GetMainViewport();
		if ( pViewport == nullptr )
			return false;

		outPos	= float2{ pViewport->Pos.x, pViewport->Pos.y };
		outSize = float2{ pViewport->Size.x, pViewport->Size.y };
		return true;
	}

	bool EditorChrome::beginSection( const EditorSectionDesc& desc )
	{
		const utf8* pId = desc._pId != nullptr ? desc._pId : "##Section";
		if ( EditorChromeInternal::s_sectionDepth < EditorChromeInternal::kMaxSectionDepth )
		{
			EditorChromeInternal::s_arrSectionStack[EditorChromeInternal::s_sectionDepth] = desc._kind;
			++EditorChromeInternal::s_sectionDepth;
		}

		if ( desc._kind == EditorSectionKind::Child )
		{
			ImVec2 childSize{ desc._childSize._x, desc._childSize._y };
			if ( ( desc._flags & EditorSectionFlags::FillRemaining ) != EditorSectionFlags::None )
				childSize.y = -ImGui::GetFrameHeightWithSpacing();

			ImGuiChildFlags childFlags = 0;
			if ( ( desc._flags & EditorSectionFlags::Border ) != EditorSectionFlags::None )
				childFlags |= ImGuiChildFlags_Borders;
			if ( ( desc._flags & EditorSectionFlags::ResizeX ) != EditorSectionFlags::None )
				childFlags |= ImGuiChildFlags_ResizeX;

			ImGuiWindowFlags windowFlags = 0;
			if ( ( desc._flags & EditorSectionFlags::HorizontalScrollbar ) != EditorSectionFlags::None )
				windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
			if ( ( desc._flags & EditorSectionFlags::NoScrollbar ) != EditorSectionFlags::None )
				windowFlags |= ImGuiWindowFlags_NoScrollbar;
			if ( ( desc._flags & EditorSectionFlags::NoScrollWithMouse ) != EditorSectionFlags::None )
				windowFlags |= ImGuiWindowFlags_NoScrollWithMouse;

			return ImGui::BeginChild( pId, childSize, childFlags, windowFlags );
		}

		ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
		ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 4.0f, 2.0f } );
		ImGui::BeginGroup();
		return true;
	}

	void EditorChrome::endSection()
	{
		EditorSectionKind kind = EditorSectionKind::Toolbar;
		if ( EditorChromeInternal::s_sectionDepth > 0 )
		{
			--EditorChromeInternal::s_sectionDepth;
			kind = EditorChromeInternal::s_arrSectionStack[EditorChromeInternal::s_sectionDepth];
		}

		if ( kind == EditorSectionKind::Child )
		{
			ImGui::EndChild();
			return;
		}

		ImGui::EndGroup();
		ImGui::PopStyleVar( 2 );
	}

	bool EditorChrome::beginFloatingBar( const EditorFloatingBarDesc& desc )
	{
		const utf8* pId = desc._pId != nullptr ? desc._pId : "##FloatingBar";

		ImGui::SetNextWindowPos( ImVec2{ desc._anchorPos._x, desc._anchorPos._y }, ImGuiCond_Always,
								 ImVec2{ desc._pivot._x, desc._pivot._y } );

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
								 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
		if ( ( desc._flags & EditorFloatingBarFlags::AutoResize ) != EditorFloatingBarFlags::None )
			flags |= ImGuiWindowFlags_AlwaysAutoResize;
		if ( ( desc._flags & EditorFloatingBarFlags::NoMove ) != EditorFloatingBarFlags::None )
		{
			flags |= ImGuiWindowFlags_NoMove;
			flags |= ImGuiWindowFlags_NoResize;
		}

		const bool bPassThrough =
			( desc._flags & EditorFloatingBarFlags::PassThroughWhenDisabled ) != EditorFloatingBarFlags::None;
		if ( desc._bEnabled == false && bPassThrough )
			flags |= ImGuiWindowFlags_NoInputs;

		const bool bVisible = ImGui::Begin( pId, nullptr, flags );
		if ( bVisible && desc._bEnabled == false )
		{
			ImGui::BeginDisabled();
			++EditorChromeInternal::s_floatingBarDisabledDepth;
		}
		return bVisible;
	}

	void EditorChrome::endFloatingBar()
	{
		if ( EditorChromeInternal::s_floatingBarDisabledDepth > 0 )
		{
			ImGui::EndDisabled();
			--EditorChromeInternal::s_floatingBarDisabledDepth;
		}
		ImGui::End();
	}

	bool EditorChrome::beginOverlay( const EditorOverlayDesc& desc )
	{
		const utf8* pId = desc._pId != nullptr ? desc._pId : "##Overlay";

		ImGui::SetNextWindowPos( ImVec2{ desc._anchorPos._x, desc._anchorPos._y }, ImGuiCond_Always,
								 ImVec2{ desc._pivot._x, desc._pivot._y } );
		if ( desc._size._x > 0.0f )
			ImGui::SetNextWindowSize( ImVec2{ desc._size._x, desc._size._y } );
		if ( desc._bgAlpha >= 0.0f )
			ImGui::SetNextWindowBgAlpha( desc._bgAlpha );

		int32 styleVarCount = 0;
		if ( desc._rounding > 0.0f )
		{
			ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, desc._rounding );
			++styleVarCount;
		}
		if ( desc._borderSize > 0.0f )
		{
			ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, desc._borderSize );
			++styleVarCount;
		}
		if ( EditorChromeInternal::s_overlayDepth < EditorChromeInternal::kMaxOverlayDepth )
		{
			EditorChromeInternal::s_arrOverlayStyleVars[EditorChromeInternal::s_overlayDepth] = styleVarCount;
			++EditorChromeInternal::s_overlayDepth;
		}

		return ImGui::Begin( pId, desc._pOpen, EditorChromeInternal::toImGuiOverlayFlags( desc._flags ) );
	}

	void EditorChrome::endOverlay()
	{
		ImGui::End();

		int32 styleVarCount = 0;
		if ( EditorChromeInternal::s_overlayDepth > 0 )
		{
			--EditorChromeInternal::s_overlayDepth;
			styleVarCount = EditorChromeInternal::s_arrOverlayStyleVars[EditorChromeInternal::s_overlayDepth];
		}
		if ( styleVarCount > 0 )
			ImGui::PopStyleVar( styleVarCount );
	}

	bool EditorChrome::beginToolbar( const utf8* pId )
	{
		EditorSectionDesc desc{};
		desc._pId  = pId != nullptr ? pId : "##Toolbar";
		desc._kind = EditorSectionKind::Toolbar;
		return beginSection( desc );
	}

	void EditorChrome::endToolbar()
	{
		endSection();
	}

	bool EditorChrome::beginSearchOverlay( const EditorSearchOverlayDesc& desc )
	{
		float2 viewportPos{};
		float2 viewportSize{};
		if ( tryGetMainViewportRect( viewportPos, viewportSize ) == false )
		{
			viewportPos	 = float2{ 0.0f, 0.0f };
			viewportSize = float2{ 800.0f, 600.0f };
		}

		EditorOverlayDesc overlayDesc{};
		overlayDesc._pId		= desc._pId != nullptr ? desc._pId : "##SearchOverlay";
		overlayDesc._pOpen		= desc._pOpen;
		overlayDesc._anchorPos	= float2{ viewportPos._x + viewportSize._x * 0.5f,
										  viewportPos._y + viewportSize._y * desc._viewportYFrac };
		overlayDesc._pivot		= float2{ 0.5f, 0.5f };
		overlayDesc._size		= desc._size;
		overlayDesc._rounding	= desc._rounding;
		overlayDesc._borderSize = desc._borderSize;
		overlayDesc._flags		= EditorOverlayFlags::NoTitleBar | EditorOverlayFlags::NoResize |
								  EditorOverlayFlags::NoMove | EditorOverlayFlags::NoSavedSettings;

		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4{ desc._bgColor._x, desc._bgColor._y, desc._bgColor._z, desc._bgColor._w } );
		ImGui::PushStyleColor( ImGuiCol_Border,
							   ImVec4{ desc._borderColor._x, desc._borderColor._y, desc._borderColor._z, desc._borderColor._w } );

		const bool bVisible = beginOverlay( overlayDesc );
		if ( bVisible == false )
			return false;

		if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) && desc._pOpen != nullptr )
			*desc._pOpen = false;

		if ( desc._pFocusOnOpen != nullptr && *desc._pFocusOnOpen == true )
		{
			ImGui::SetKeyboardFocusHere();
			*desc._pFocusOnOpen = false;
		}
		return true;
	}

	void EditorChrome::endSearchOverlay()
	{
		endOverlay();
		ImGui::PopStyleColor( 2 );
	}
} // namespace sw::editor
