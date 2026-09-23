/**
 * @file RHI.h
 * @brief RHI 팩토리와 플랫폼 도우미입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    class CommandLineManager;
    class IRHIDevice;
} // namespace sw

namespace sw
{
    class IRenderSurface;

    extern SW_API RHIBackend gv_rhiBackend;

    /**
     * @struct RHIBackendUtil
     * @brief 백엔드 선택에 관한 질문 중 **한 자리에 있어야 하는 것**을 듭니다.
     */
    struct SW_API RHIBackendUtil
    {
        /**
         * @brief 커맨드라인이 백엔드를 **명시했는지**, 명시했다면 무엇인지 반환합니다.
         * @details 이 질문은 두 곳에서 필요합니다. `EngineLoop` 은 "명시했는가"(안 했으면 설정
         *          기본값으로 덮는다), `RHI::initialize` 는 "무엇인가"(와, 쓸 수 없을 때 조용히
         *          폴백해도 되는가)를 묻습니다. 예전에는 네 플래그를 훑는 같은 사슬이 **두 벌로** 적혀 있었고,
         *          그래서 이미 답이 갈려 있었습니다: `EngineLoop` 쪽만 `-gv_rhiBackend` 를 명시로 쳤고
         *          `RHI` 쪽은 아니어서, 쓸 수 없는 백엔드를 `-gv_rhiBackend` 로 고르면 에러 없이
         *          다른 백엔드로 떴습니다. `-vk` 로 같은 것을 고르면 에러였습니다.
         * @param commandLineManager 파싱이 끝난 커맨드라인
         * @param outBackend 명시했을 때만 채워집니다
         * @return 커맨드라인이 백엔드를 명시했으면 true
         */
        static bool findCommandLineBackend( const CommandLineManager& commandLineManager, RHIBackend& outBackend );
    };

    /**
     * @class RHI
     * @brief 플랫폼 · 백엔드에 맞는 IRHIDevice 를 만드는 팩토리입니다.
     */
    class SW_API RHI
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 복사/이동 금지, initialize/shutdown, 소프트 재생성
        // ------------------------------------------------------------------------------
        /** @brief 디바이스 없이 만듭니다. */
        RHI();
        /** @brief 디바이스를 정리합니다. */
        ~RHI();

        /** @brief 복사를 금지합니다. */
        RHI( const RHI& ) = delete;
        /** @brief 대입을 금지합니다. */
        RHI& operator=( const RHI& ) = delete;
        /** @brief 이동을 금지합니다. */
        RHI( RHI&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        RHI& operator=( RHI&& ) = delete;

        /**
         * @brief 기본 백엔드로 디바이스를 초기화합니다.
         * @param pSurface 스왑체인을 걸 표면(보통 활성 `IWindow`). RHI 는 창 시스템을 모르므로 부르는 쪽이 넘깁니다.
         *                 백엔드 교체(`recreateDevice`)도 같은 표면을 다시 씁니다.
         */
        bool initialize( IRenderSurface* pSurface );
        /** @brief CLI 에 --VSYNC 가 없을 때 쓸 스왑체인 VSync 를 정합니다. initialize 전에 부릅니다. */
        void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync ? SW_TRUE : SW_FALSE; }
        /** @brief 디바이스를 종료하고 모듈을 언로드합니다. */
        void shutdown();

        /** @brief 앱 재시작 없이 현재 디바이스를 파괴하고 백엔드를 다시 만듭니다. */
        bool recreateDevice( RHIBackend backend );

        /** @brief gv_rhiBackend 가 바뀌면 백엔드 교체를 예약합니다. */
        void schedulePendingBackendChange( RHIBackend requested );
        /** @brief 대기 중인 백엔드 변경이 있는지 반환합니다. */
        bool hasPendingBackendChange() const { return _bPendingBackendChange == SW_TRUE; }
        /** @brief 대기 중인 백엔드를 가져가고 플래그를 해제합니다. */
        RHIBackend consumePendingBackendChange();
        /** @brief 커밋된 백엔드를 반환합니다. */
        RHIBackend getCommittedBackend() const { return _committedRHIBackend; }

        // ------------------------------------------------------------------------------
        // 2) 팩토리 — 백엔드 생성, 표시 이름, 플랫폼 기본, 셰이더 타깃
        // ------------------------------------------------------------------------------
        /**
         * @brief 지정 백엔드(DirectX11, DirectX12, Vulkan, OpenGL)의 RHI 디바이스를 만듭니다.
         * @param backend 생성할 백엔드 종류
         * @return 생성된 IRHIDevice unique_ptr (실패 시 nullptr)
         */
        static unique_ptr<IRHIDevice> createDevice( RHIBackend backend );

        /** @brief 백엔드 열거형의 표시용 이름을 반환합니다. */
        static const utf8* getBackendTypeName( RHIBackend backend );

        /** @brief 현재 OS 의 기본 RHI 백엔드를 반환합니다(Windows 는 DX12, Linux 는 Vulkan, 그 밖에는 OpenGL). */
        static RHIBackend getDefaultPlatformBackend();

        /** @brief 해당 RHI 백엔드의 셰이더 타깃 포맷(DXIL, SPIR-V, DXBC 등)을 반환합니다. */
        static ShaderTargetFormat getShaderTargetFormat( RHIBackend backend );

        // ------------------------------------------------------------------------------
        // 3) 조회 — initialize 이후
        // ------------------------------------------------------------------------------
        /** @brief 디바이스가 있는지 반환합니다. */
        bool hasDevice() const { return _device != nullptr; }

        /** @brief 활성 IRHIDevice 를 반환합니다. */
        IRHIDevice& getDevice() const { return *_device; }

    private:
        unique_ptr<IRHIDevice> _device;
        IRenderSurface*        _pSurface;

        RHIBackend             _pendingRHIBackend;
        RHIBackend             _committedRHIBackend;
        uint8                  _bPreferredVSync       : 1;
        uint8                  _bPendingBackendChange : 1;
        [[maybe_unused]] uint8 _reserved              : 6;
    };
} // namespace sw
