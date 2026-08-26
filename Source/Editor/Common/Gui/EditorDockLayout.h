/**
 * @file EditorDockLayout.h
 * @brief 에디터 도크스페이스 · 기본 레이아웃 · imgui.ini / windows.ini 지속성
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	/** @brief 메인 도크 레이아웃과 패널 가시성 저장 */
	class EditorDockLayout
	{
	public:
		EditorDockLayout();

		/** @brief Config/Editor 아래 imgui.ini / windows.ini 경로를 해석합니다. */
		void setupPersistencePaths();
		/** @brief 해석된 imgui.ini 경로를 ImGui IO에 연결합니다. */
		void applyIniFilename() const;
		/** @brief windows.ini에서 패널 열림 상태를 복원합니다. */
		void loadPanelVisibility();
		/** @brief 패널 가시성과 ImGui 도크 레이아웃을 저장합니다. */
		void save();
		/** @brief 메인 도크스페이스를 열고, 비어 있으면 기본 레이아웃을 적용합니다. */
		void beginDockspace();
		/** @brief 다음 프레임에 기본 도크 레이아웃을 다시 적용합니다. */
		void requestResetDefault();

	private:
		void applyDefaultDockLayout( uint32 dockspaceId );

		string				   _imguiIniPath;
		string				   _windowsIniPath;
		uint8				   _bApplied : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw
