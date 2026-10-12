/**
 * @file PortraitBatchUtil.h
 * @brief 프리팹 목록마다 초상화를 그려 `<이름>.portrait.dds` · `.png` 로 씁니다(`--render-portraits`, `EngineLoop::renderPortraits`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;
    class RenderThread;

    /**
     * @struct PortraitBatchUtil
     * @brief 초상화 일괄 그리기입니다. 한 장을 그리는 일은 `PortraitRenderer` 가 하고, 여기는 목록 · 컨텍스트 · 파일 쓰기를 맡습니다.
     */
    struct SW_API PortraitBatchUtil
    {
        /**
         * @brief @p prefabList(쉼표로 여럿)의 프리팹마다 초상화를 그려 @p outputDirectory(비면 `Saved/Portraits`)에 씁니다. 하나라도 실패하면 false 입니다.
         * @details 렌더 스레드가 받은 일을 모두 끝내게 한 뒤, 컨텍스트가 스레드에 묶이는 백엔드(GL · DX11)는 이 스레드가 잡고 그립니다.
         *          모듈(게임 컴포넌트 타입)이 올라온 뒤에 부릅니다. @p size 는 16 ~ 4096 으로 자릅니다.
         */
        [[nodiscard]] static bool renderPrefabList( IRHIDevice& device, RenderThread* pRenderThread, string_view prefabList, string_view outputDirectory,
                                                    uint32 size );
        /** @brief 초상화 파일 이름의 줄기입니다 — 경로의 마지막 조각에서 첫 점 앞까지(`game/heroes/hero.prefab.xml` → `hero`). */
        static string_view makeFileStem( string_view prefabPath );
    };
} // namespace sw
