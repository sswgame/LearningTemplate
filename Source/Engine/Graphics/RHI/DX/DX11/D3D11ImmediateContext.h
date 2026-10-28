/**
 * @file D3D11ImmediateContext.h
 * @brief D3D11 즉시 컨텍스트를 런타임 임계 구역과 함께 소유하고, 잠근 가드로만 꺼내 줍니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <d3d11_4.h>

namespace sw
{
    class D3D11RHIDevice;

    /**
     * @class D3D11ImmediateContext
     * @brief 즉시 컨텍스트(`ID3D11DeviceContext`)와 그것을 지키는 런타임 임계 구역(`ID3D11Multithread`)입니다. 백엔드 코드는 `lock()` 의 가드로만 컨텍스트를 꺼냅니다.
     * @details `ID3D11DeviceContext` 는 스레드 안전하지 않다. 렌더 스레드의 프레임 스트림과 다른 스레드의 전송 제출 · `pollTransfer` · `waitIdle` 이
     *          같은 즉시 컨텍스트로 나간다. `initialize` 가 `SetMultithreadProtected( TRUE )` 를 켜서 런타임이 호출 하나하나를 임계 구역으로 감싸고,
     *          `lock()` 은 같은 임계 구역(`Enter` · `Leave`)을 잡아 여러 호출과 그것에 딸린 백엔드 상태(전송 큐 · 쿼리 풀 · 프레임 링)를 한 덩어리로 묶는다.
     *          락이 하나뿐이라 렌더 스레드의 호출도 다른 스레드의 덩어리 사이에 끼지 못한다. 임계 구역은 재귀라 같은 스레드가 다시 잡아도 된다.
     *          가드 없이 꺼내는 길(`getPerCallProtected`)은 `D3D11RHIDevice` 에만 열려 있다 — 렌더 스레드의 프레임 스트림 컨텍스트가 쓰고, 그 호출은 런타임이
     *          하나씩 지킨다. 백엔드 밖(ImGui 백엔드)으로는 `IRHIDevice::withNativeContext` 가 `lock()` 을 쥔 채로만 내준다. 파이프라인 상태(렌더 타깃 · 셰이더)는
     *          컨텍스트에 하나라 그것을 바꾸는 쪽은 렌더 스레드뿐이다. 프레임 스트림만 쓰는 버퍼(루트 상수 CB)의 `Map` · `Unmap` 짝은 사이에 다른 스레드의 호출이 끼어도 괜찮고,
     *          다른 스레드도 Map 할 수 있는 버퍼(`updateConstantBuffer`)는 프레임 스트림도 `lock()` 으로 묶는다.
     */
    class D3D11ImmediateContext
    {
        friend class D3D11RHIDevice;

    public:
        /** @brief 즉시 컨텍스트의 임계 구역을 쥔 동안만 컨텍스트를 내주는 가드입니다. 옮기거나 복사하지 않습니다. */
        class ScopedLock
        {
            friend class D3D11ImmediateContext;

        public:
            ~ScopedLock();
            ScopedLock( const ScopedLock& )            = delete;
            ScopedLock& operator=( const ScopedLock& ) = delete;
            ScopedLock( ScopedLock&& )                 = delete;
            ScopedLock& operator=( ScopedLock&& )      = delete;

            ID3D11DeviceContext* operator->() const { return _pContext; }
            ID3D11DeviceContext* get() const { return _pContext; }

        private:
            ScopedLock( ID3D11DeviceContext* pContext, ID3D11Multithread* pMultithread );

            ID3D11DeviceContext* _pContext;
            ID3D11Multithread*   _pMultithread;
        };

        /** @brief 빈 상태로 만듭니다. */
        D3D11ImmediateContext();

        /** @brief 즉시 컨텍스트를 받아 런타임의 다중 스레드 보호를 켭니다. 보호를 켜지 못하면 오류를 남기고 false 입니다(폴백 없음). */
        [[nodiscard]] bool initialize( Microsoft::WRL::ComPtr<ID3D11DeviceContext> context );
        /** @brief 컨텍스트와 임계 구역 인터페이스를 놓습니다. */
        void shutdown();

        /** @brief 즉시 컨텍스트가 있으면 true 입니다. */
        bool isValid() const { return _context != nullptr; }
        /** @brief 임계 구역을 잡고 가드를 돌려줍니다. `isValid()` 일 때만 부릅니다. */
        [[nodiscard]] ScopedLock lock() const;

    private:
        /** @brief 가드 없이 컨텍스트를 꺼냅니다. 런타임이 호출마다 잠그므로 호출 하나는 안전하고, 여러 호출을 묶는 일은 `lock()` 으로 합니다. */
        ID3D11DeviceContext* getPerCallProtected() const { return _context.Get(); }

        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _context;
        Microsoft::WRL::ComPtr<ID3D11Multithread>   _multithread;
    };
} // namespace sw

#endif
