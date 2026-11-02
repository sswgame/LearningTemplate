/**
 * @file RHIDrawDiagnostics.h
 * @brief 백엔드가 드로우를 기록하다 만난 잘못된 바인딩을 네 백엔드가 같은 판정과 같은 문구로 알리는 곳입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct RHIGeometryBindings;
    struct RHIVertexLayout;

    /**
     * @struct RHIDrawDiagnostics
     * @brief 드로우 기록의 진단입니다. 판정은 백엔드가, 알림 문구와 횟수 제한은 여기가 맡습니다.
     */
    struct SW_API RHIDrawDiagnostics
    {
        /** @brief 처음 이만큼만 로그를 남기고 그 뒤는 세기만 합니다(프레임마다 수천 줄이 쌓이지 않게). */
        static constexpr uint32 kMaxReportedDraw = 8;

        /**
         * @brief 드로우가 설정한 메시 정점 버퍼가 해제돼 핸들로 찾을 수 없어 그 드로우를 버렸음을 알립니다.
         * @details 네 백엔드 모두 그 드로우를 버린다. 다른 버퍼나 직전 드로우의 버퍼로 대신 그리지 않는다.
         *          횟수는 백엔드 이름마다 따로 센다. 백엔드를 바꾸면 그 백엔드는 다시 처음부터 센다.
         */
        static void reportDestroyedVertexBuffer( const utf8* pBackendName, RHIBufferHandle handle );
        /** @brief 렌더 패스가 열려 있지 않아 그 드로우를 버렸음을 알립니다(패스 밖 드로우가 없는 백엔드 — Vulkan). 열린 패스를 무언가가 닫았거나 패스를 열지 않고 그렸다. */
        static void reportDrawWithoutRenderPass( const utf8* pBackendName );
        /** @brief 인덱스 드로우에 인덱스 버퍼를 걸지 않아 그 드로우를 버렸음을 알립니다(네 백엔드가 같다 — 앞 드로우의 인덱스 버퍼로 그리지 않는다). */
        static void reportIndexBufferUnbound( const utf8* pBackendName );
        /** @brief 인덱스 드로우에 건 인덱스 버퍼가 해제돼 핸들로 찾을 수 없어 그 드로우를 버렸음을 알립니다. */
        static void reportDestroyedIndexBuffer( const utf8* pBackendName, RHIBufferHandle handle );

        /**
         * @brief PSO 가 읽는 정점 버퍼 슬롯(@p input 의 슬롯, `RHIShaderRequest::toVertexSlotMask`)에 버퍼가 모두 걸렸고 원소 크기가 PSO 의 메시 정점 형식과 같은지 봅니다.
         *        어긋나면 알리고 false 입니다.
         * @details 네 백엔드가 그 드로우를 버린다. 다른 버퍼로 대신 그리지 않는다 — 정점 입력이 없는 셰이더는 입력 레이아웃이 비어 슬롯을 읽지 않으므로
         *          (언리얼의 빈 정점 선언 · Godot 의 빈 정점 포맷) 빈 슬롯은 메시를 걸지 않고 메시 셰이더로 그린 부르는 쪽의 결함이다. 원소 크기가 다르면
         *          메시의 정점 형식과 PSO 의 형식이 다르다(지형 메시를 일반 형식 PSO 로 그렸다) — 그대로 그리면 속성을 엉뚱한 바이트에서 읽는다.
         */
        [[nodiscard]] static bool checkVertexSlotsBound( const utf8* pBackendName, const RHIVertexLayout& input, const RHIGeometryBindings& bindings );
    };
} // namespace sw
