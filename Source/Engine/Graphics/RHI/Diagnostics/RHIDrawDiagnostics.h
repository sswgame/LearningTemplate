/**
 * @file RHIDrawDiagnostics.h
 * @brief 백엔드가 드로우를 기록하다 만난 잘못된 바인딩을 네 백엔드가 같은 판정과 같은 문구로 알리는 곳입니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    struct RHIGeometryBindings;
    struct RHIVertexLayout;

    /**
     * @class RHIDrawDiagnostics
     * @brief 드로우 기록의 진단입니다. 판정은 백엔드가, 알림 문구와 횟수 제한은 여기가 맡습니다. 디바이스가 하나씩 든다(`IRHIDevice::getDrawDiagnostics`).
     * @details 횟수는 디바이스마다 센다 — 백엔드를 바꾸면(새 디바이스) 다시 처음부터 센다. 예전의 백엔드 이름 → 횟수 정적 맵 대신 디바이스 멤버다
     *          (언리얼도 RHI 검증 상태를 동적 RHI 객체가 든다). 기록 스레드가 여럿이라 횟수는 원자값이고, 알림은 잘못된 드로우를 만난 경로에서만 부른다.
     *          알림이 진단 상태(횟수)만 바꾸므로 const 로 부른다(`IRHICommandContext::isDrawAllowed` 가 const 다).
     */
    class SW_API RHIDrawDiagnostics
    {
    public:
        /** @brief 처음 이만큼만 로그를 남기고 그 뒤는 세기만 합니다(프레임마다 수천 줄이 쌓이지 않게). */
        static constexpr uint32 kMaxReportedDraw = 8;

        /** @param owner 알림 줄에 백엔드 이름(`IRHIDevice::getBackendName`)을 붙일 디바이스 — 생성자에서는 이름을 읽지 않는다(가상 함수). */
        explicit RHIDrawDiagnostics( const IRHIDevice& owner );
        RHIDrawDiagnostics( const RHIDrawDiagnostics& )            = delete;
        RHIDrawDiagnostics& operator=( const RHIDrawDiagnostics& ) = delete;

        /**
         * @brief 드로우가 설정한 메시 정점 버퍼가 해제돼 핸들로 찾을 수 없어 그 드로우를 버렸음을 알립니다.
         * @details 네 백엔드 모두 그 드로우를 버린다. 다른 버퍼나 직전 드로우의 버퍼로 대신 그리지 않는다.
         */
        void reportDestroyedVertexBuffer( RHIBufferHandle handle ) const;
        /** @brief 렌더 패스가 열려 있지 않아 그 드로우를 버렸음을 알립니다(패스 밖 드로우가 없는 백엔드 — Vulkan). 열린 패스를 무언가가 닫았거나 패스를 열지 않고 그렸다. */
        void reportDrawWithoutRenderPass() const;
        /** @brief 렌더 스레드가 렌더 패스를 연 채로 버퍼를 갱신했음을 알립니다(`IRHIDevice::requireNoOpenRenderPassForUpload` — 업로드는 그리기 기록 전에). */
        void reportBufferUpdateInsideRenderPass( const utf8* pWhat ) const;
        /** @brief 인덱스 드로우에 인덱스 버퍼를 걸지 않아 그 드로우를 버렸음을 알립니다(네 백엔드가 같다 — 앞 드로우의 인덱스 버퍼로 그리지 않는다). */
        void reportIndexBufferUnbound() const;
        /** @brief 인덱스 드로우에 건 인덱스 버퍼가 해제돼 핸들로 찾을 수 없어 그 드로우를 버렸음을 알립니다. */
        void reportDestroyedIndexBuffer( RHIBufferHandle handle ) const;

        /**
         * @brief PSO 가 읽는 정점 버퍼 슬롯(@p input 의 슬롯, `RHIShaderRequest::toVertexSlotMask`)에 버퍼가 모두 걸렸고 원소 크기가 PSO 의 메시 정점 형식과 같은지 봅니다.
         *        어긋나면 알리고 false 입니다.
         * @details 네 백엔드가 그 드로우를 버린다. 다른 버퍼로 대신 그리지 않는다 — 정점 입력이 없는 셰이더는 입력 레이아웃이 비어 슬롯을 읽지 않으므로
         *          (언리얼의 빈 정점 선언 · Godot 의 빈 정점 포맷) 빈 슬롯은 메시를 걸지 않고 메시 셰이더로 그린 부르는 쪽의 결함이다. 원소 크기가 다르면
         *          메시의 정점 형식과 PSO 의 형식이 다르다(지형 메시를 일반 형식 PSO 로 그렸다) — 그대로 그리면 속성을 엉뚱한 바이트에서 읽는다.
         */
        [[nodiscard]] bool checkVertexSlotsBound( const RHIVertexLayout& input, const RHIGeometryBindings& bindings ) const;

    private:
        /** @brief 횟수를 1 증가시키고 증가 전 값을 돌려줍니다. */
        uint32 countReport() const;
        /** @brief 알림 줄에 붙일 백엔드 이름입니다. */
        const utf8* getBackendName() const;

    private:
        const IRHIDevice*      _pOwner;      ///< 이 진단을 든 디바이스(이름만 읽는다)
        mutable atomic<uint32> _reportCount; ///< 지금까지 알린(센) 잘못된 드로우 수 — 진단 상태라 const 알림이 올린다
    };
} // namespace sw
