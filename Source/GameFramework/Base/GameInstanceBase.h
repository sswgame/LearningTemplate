/**
 * @file GameInstanceBase.h
 * @brief IGame 공통 수명 + BootstrapConfig 배선 및 인스턴스 상태 스냅샷 직렬화
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/IGame.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct TypeInfo;

    // ------------------------------------------------------------------------------
    // 1) GameInstanceBase — 수명주기는 여기, 팩 루트만 파생
    //    EmptyGame처럼 최소 모듈이 configureBootstrap만 오버라이드
    // ------------------------------------------------------------------------------
    /** @brief IGame 수명주기를 고정하고 부트스트랩 및 런타임 상태 스냅샷 직렬화를 지원합니다. */
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

        // --------------------------------------------------------------------------
        // 런타임 상태 직렬화 / 스냅샷 (Dev LiveReload & Shipping 체크포인트/세이브)
        // --------------------------------------------------------------------------
        /** @brief C-ABI: 활성 씬과 파생 클래스의 커스텀 리플렉션 상태를 통합 바이너리로 직렬화합니다. */
        bool serializeState( void* pOutBuffer, uint32* pInOutSize ) override;

        /** @brief C-ABI: 바이너리 버퍼에서 씬과 커스텀 리플렉션 상태를 복원합니다. */
        bool deserializeState( const void* pInBuffer, uint32 size ) override;

        /** @brief Shipping/Gameplay: 현재 씬과 게임 상태를 인메모리 스냅샷 버퍼에 캡처합니다 (체크포인트/타임리와인드용). */
        bool captureSnapshot( vector<uint8>& outBytes );

        /** @brief Shipping/Gameplay: 인메모리 스냅샷 버퍼로부터 씬과 게임 상태를 즉시 복원합니다. */
        bool restoreSnapshot( const vector<uint8>& inBytes );

        /** @brief Shipping/Gameplay: 씬과 게임 상태 전체를 바이너리 파일로 저장합니다. */
        bool saveStateToFile( string_view filePath );

        /** @brief Shipping/Gameplay: 바이너리 파일로부터 씬과 게임 상태 전체를 복원합니다. */
        bool loadStateFromFile( string_view filePath );

    protected:
        /** @brief 파생 클래스가 팩 루트/부트스트랩을 설정합니다. */
        virtual void configureBootstrap( BootstrapConfig& outConfig ) { (void)outConfig; }
        /** @brief 부트스트랩 로드 후 파생 초기화 훅 */
        virtual bool onInitialize() { return true; }
        /** @brief 파생 종료 훅 */
        virtual void onShutdown() {}
        /** @brief 파생 프레임 훅 */
        virtual void onUpdate( float32 deltaTime ) { (void)deltaTime; }

        // --------------------------------------------------------------------------
        // 파생 클래스 전용 상태 스냅샷 리플렉션 훅
        // --------------------------------------------------------------------------
        /** @brief 파생 클래스의 리플렉션 상태 TypeInfo를 반환합니다 (미정의 시 씬만 직렬화). */
        virtual const TypeInfo* getStateTypeInfo() const { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 인스턴스 포인터를 반환합니다. */
        virtual void* getStateInstance() { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 const 인스턴스 포인터를 반환합니다. */
        virtual const void* getStateInstance() const { return nullptr; }

        /** @brief 상태 스냅샷 직렬화 직전 호출되는 준비 훅 */
        virtual void onBeforeStateSerialize() {}
        /** @brief 상태 스냅샷 역직렬화 직후 호출되는 복원 훅 */
        virtual void onAfterStateDeserialize() {}

        /** @brief 씬 내부의 모든 유효 GameObject를 바이너리로 직렬화합니다. */
        bool serializeSceneObjects( vector<uint8>& outBytes );
        /** @brief 바이너리 데이터로부터 씬 GameObject들을 복원합니다. */
        bool deserializeSceneObjects( const uint8* pData, size_t size );

        BootstrapConfig _bootstrap{};           ///< 팩 루트와 gamedata
        IWindow*        _pWindow{ nullptr };    ///< 호스트 윈도우 (App이 소유)
        IRHIDevice*     _pRhiDevice{ nullptr }; ///< 활성 RHI 디바이스
    };
} // namespace sw
