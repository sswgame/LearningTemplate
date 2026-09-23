/**
 * @file GameInstanceBase.h
 * @brief IGame 공통 수명주기 + BootstrapConfig 배선과 인스턴스 상태 스냅샷 직렬화입니다.
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
    // 1) GameInstanceBase — 수명주기는 여기, 파생은 훅만
    //    파생은 configureBootstrap 과 on* 훅을 오버라이드한다(예: EmptyGame)
    // ------------------------------------------------------------------------------
    /** @brief IGame 수명주기를 고정하고 부트스트랩 및 런타임 상태 스냅샷 직렬화를 지원합니다. */
    class SW_GF_API GameInstanceBase : public IGame
    {
    public:
        /** @brief 윈도우 · RHI · 부트스트랩을 비운 채로 둡니다. */
        GameInstanceBase() = default;
        /** @brief 파생 인스턴스가 정리할 수 있게 합니다. */
        virtual ~GameInstanceBase() override = default;

        /** @brief configureBootstrap 으로 부트스트랩을 채우고(GameConfig 의 팩 루트가 있으면 그것이 우선) gamedata 를 읽은 뒤 onInitialize 를 부릅니다. */
        bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) final;
        /** @brief onShutdown 뒤에 윈도우 · RHI 포인터를 끊습니다. */
        void shutdown() final;
        /** @brief onUpdate 로 한 프레임을 넘깁니다. */
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
        /** @brief 파생 클래스가 팩 루트 · 부트스트랩을 설정합니다. */
        virtual void configureBootstrap( BootstrapConfig& outConfig ) { (void)outConfig; }
        /** @brief 부트스트랩을 읽은 뒤 부르는 파생 초기화 훅입니다. */
        virtual bool onInitialize() { return true; }
        /** @brief 파생 종료 훅입니다. */
        virtual void onShutdown() {}
        /** @brief 파생 프레임 훅입니다. */
        virtual void onUpdate( float32 deltaTime ) { (void)deltaTime; }

        // --------------------------------------------------------------------------
        // 파생 클래스 전용 상태 스냅샷 리플렉션 훅
        // --------------------------------------------------------------------------
        /** @brief 파생 클래스의 리플렉션 상태 TypeInfo 를 반환합니다(없으면 씬만 직렬화합니다). */
        virtual const TypeInfo* getStateTypeInfo() const { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 인스턴스 포인터를 반환합니다. */
        virtual void* getStateInstance() { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 const 인스턴스 포인터를 반환합니다. */
        virtual const void* getStateInstance() const { return nullptr; }

        /** @brief 상태 스냅샷 직렬화 직전에 부르는 준비 훅입니다. */
        virtual void onBeforeStateSerialize() {}
        /** @brief 상태 스냅샷 역직렬화 직후에 부르는 복원 훅입니다. */
        virtual void onAfterStateDeserialize() {}

        /** @brief `deserializeSceneObjects` 가 읽는 씬 오브젝트 데이터의 형식입니다. */
        enum class SceneObjectFormat : uint8
        {
            StateOnly,      ///< 오브젝트마다 상태만 있습니다(봉투 v1 과 그 이전). 새 id 를 받습니다.
            WithIdentity,   ///< 오브젝트마다 id + 상태. 다른 실행에서 찍은 것이라 id 는 읽고 버립니다(새 id).
            RestoreIdentity ///< 오브젝트마다 id + 상태. 같은 프로세스에서 찍었으므로 원래 id 를 되살립니다(핫 리로드).
        };

        /**
         * @brief 씬 안의 모든 유효 GameObject 를 바이너리로 직렬화합니다.
         * @details 오브젝트마다 상태 앞에 런타임 id(`ObjectIdentity`)를 싣습니다. 같은 프로세스에서 되살리면(핫 리로드)
         *          `GameObjectHandle` · `ComponentHandle` 이 그 너머로도 이어집니다.
         */
        bool serializeSceneObjects( vector<uint8>& outBytes );
        /** @brief 바이너리 데이터로부터 씬 GameObject 들을 복원합니다. @p format 은 id 가 실렸는지, 되살릴지를 알려 줍니다. */
        bool deserializeSceneObjects( const uint8* pData, size_t size, SceneObjectFormat format );

        BootstrapConfig _bootstrap{};           ///< 팩 루트와 gamedata
        IWindow*        _pWindow{ nullptr };    ///< 호스트 윈도우 (App 이 소유)
        IRHIDevice*     _pRhiDevice{ nullptr }; ///< 활성 RHI 디바이스
    };
} // namespace sw
