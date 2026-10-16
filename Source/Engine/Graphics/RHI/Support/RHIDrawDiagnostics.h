/**
 * @file RHIDrawDiagnostics.h
 * @brief 백엔드가 드로우를 기록하다 만난 잘못된 바인딩을 네 백엔드가 같은 판정과 같은 문구로 알리는 곳입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
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
         * @details 네 백엔드 모두 그 드로우를 버린다. 풀스크린 버퍼나 직전 드로우의 버퍼로 대신 그리지 않는다.
         *          횟수는 백엔드 이름마다 따로 센다. 백엔드를 바꾸면 그 백엔드는 다시 처음부터 센다.
         */
        static void reportDestroyedVertexBuffer( const utf8* pBackendName, RHIBufferHandle handle );
    };
} // namespace sw
