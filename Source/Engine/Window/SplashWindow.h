/**
 * @file SplashWindow.h
 * @brief 애플리케이션 및 에디터 초기화 중 표시되는 네이티브 경량 스플래시 화면 RAII 래퍼
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class ISplashWindow;

	/**
	 * @class SplashWindow
	 * @brief 엔진/모듈 로딩 중 화면 중앙에 즉시 나타나는 플랫폼 독립적인 경량 스플래시 창
	 */
	class SW_API SplashWindow
	{
	public:
		SplashWindow();
		~SplashWindow();

		SplashWindow( const SplashWindow& )			   = delete;
		SplashWindow& operator=( const SplashWindow& ) = delete;

		/**
		 * @brief 스플래시 창을 생성하여 화면 중앙에 즉시 표시합니다.
		 * @param pTitle 상단 타이틀 텍스트 (예: "SW Engine")
		 * @param pInitialStatus 하단 상태 텍스트 (예: "Initializing Engine...")
		 * @param width 창 가로 크기 (기본 480)
		 * @param height 창 세로 크기 (기본 280)
		 */
		bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width = 480, uint32 height = 280 );

		/** @brief 현재 진행 상황 텍스트를 업데이트하고 화면을 즉시 갱신합니다. */
		void updateStatus( const utf8* pStatus );

		/** @brief 스플래시 창을 닫고 리소스를 해제합니다. */
		void dismiss();

		/** @brief 스플래시 창이 현재 열려 있는지 확인합니다. */
		bool isOpen() const;

	private:
		unique_ptr<ISplashWindow> _pImpl;
	};
} // namespace sw
