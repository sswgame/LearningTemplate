#pragma once
/**
 * @file EditorWidgets.h
 * @brief Shared ImGui widgets for editor windows (Hazel/Spartan look, reimplemented)
 */
#include "Core/Common/Types.h"
#include "Core/Utility/Math/VectorMath.h"

namespace sw::editor
{
	struct Color4
	{
		float32 r = 1.0f;
		float32 g = 1.0f;
		float32 b = 1.0f;
		float32 a = 1.0f;
	};

	namespace style
	{
		inline constexpr Color4 kAxisX{ 0.80f, 0.10f, 0.15f, 1.0f };
		inline constexpr Color4 kAxisY{ 0.20f, 0.70f, 0.20f, 1.0f };
		inline constexpr Color4 kAxisZ{ 0.10f, 0.25f, 0.80f, 1.0f };
		inline constexpr Color4 kAccent{ 0.78f, 0.22f, 0.18f, 1.0f };
		inline constexpr Color4 kHeader{ 0.22f, 0.35f, 0.48f, 1.0f };
		inline constexpr Color4 kOk{ 0.20f, 0.65f, 0.30f, 1.0f };
		inline constexpr Color4 kWarn{ 0.85f, 0.65f, 0.15f, 1.0f };
	} // namespace style

	/** @brief Label | RGB axis buttons (reset) | DragFloat x3. Returns true if edited. */
	bool drawVec3Control( const char* label, float3& values, float32 resetValue = 0.0f, float32 columnWidth = 100.0f,
						  float32 speed = 0.1f );

	/** @brief Framed collapsible component card. Returns true while body should draw. Call endComponentCard if true. */
	bool beginComponentCard( const char* name, uint64 id, bool* bActive, bool* bRemoveRequested, bool bAccent = false );
	void endComponentCard();

	void drawPanelHeader( const char* title, const char* subtitle = nullptr );
	void drawChip( const char* label, const Color4& color );
	bool drawPropertyRowBegin( const char* label, float32 labelWidth = 120.0f );
	void drawPropertyRowEnd();

	void pushInspectorStyle();
	void popInspectorStyle();
} // namespace sw::editor
