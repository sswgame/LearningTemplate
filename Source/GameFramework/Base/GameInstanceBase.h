/**
 * @file GameInstanceBase.h
 * @brief IGame 공통 수명 + BootstrapConfig 배선
 */
#pragma once
#include "GameFramework/Base/IGame.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) GameInstanceBase — 수명주기는 여기, 팩 루트만 파생
	//    EmptyGame처럼 최소 모듈이 configureBootstrap만 오버라이드
	// ------------------------------------------------------------------------------
	/** @brief IGame 수명주기를 고정하고 부트스트랩만 파생에 맡깁니다. */
	class SW_GF_API GameInstanceBase : public IGame
	{
	public:
		/** @brief 윈도우·RHI·부트스트랩을 비운 채로 둡니다. */
		GameInstanceBase() = default;
		/** @brief 파생 인스턴스가 정리할 수 있게 합니다. */
		virtual ~GameInstanceBase() override = default;

		/** @brief 부트스트랩을 로드한 뒤 onInitialize를 호출합니다. */
		bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) final;
		/** @brief onShutdown 후 윈도우·RHI 포인터를 끊습니다. */
		void shutdown() final;
		/** @brief onUpdate로 한 프레임을 넘깁니다. */
		void update( float32 deltaTime ) final;

		/** @brief 활성 씬의 GameObject만 바이너리로 직렬화합니다. 게임플레이 상태는 파생 클래스가 덧붙입니다. */
		bool serializeState( void* pOutBuffer, uint32* pInOutSize ) override;

		/** @brief 버퍼에서 씬 GameObject를 복원합니다. */
		bool deserializeState( const void* pInBuffer, uint32 size ) override;

	protected:
		/** @brief 파생 클래스가 팩 루트/부트스트랩을 설정합니다. */
		virtual void configureBootstrap( BootstrapConfig& outConfig ) { (void)outConfig; }
		/** @brief 부트스트랩 로드 후 파생 초기화 훅 */
		virtual bool onInitialize() { return true; }
		/** @brief 파생 종료 훅 */
		virtual void onShutdown() {}
		/** @brief 파생 프레임 훅 */
		virtual void onUpdate( float32 deltaTime ) { (void)deltaTime; }

		BootstrapConfig _bootstrap{};			///< 팩 루트와 gamedata
		IWindow*		_pWindow{ nullptr };	///< 호스트 윈도우 (App이 소유)
		IRHIDevice*		_pRhiDevice{ nullptr }; ///< 활성 RHI 디바이스
	};
} // namespace sw
