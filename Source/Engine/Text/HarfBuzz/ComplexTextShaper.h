/**
 * @file ComplexTextShaper.h
 * @brief 복합 문자 셰이퍼(아랍어 연결형 · 인도계 문자 · 합자 · 결합 분음)를 만드는 창구입니다. 구현은 이 폴더 안에서만 셰이핑 라이브러리를 봅니다.
 * @details 구성 옵션 `SW_ENABLE_COMPLEX_TEXT`(cmake/Config/BuildOptions.cmake)가 켜진 빌드에만 있습니다(`SW_TEXT_COMPLEX_SHAPING`). 꺼진 빌드는
 *          `ITextShaper::createDefault` 가 단순 셰이퍼를 고릅니다 — 실행 중에 고르지 않고 빌드가 정한다(조용히 바뀌는 길이 없다).
 *          라이브러리 헤더 · 타입은 이 폴더 밖으로 나가지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/ITextShaper.h"

namespace sw
{
    /**
     * @struct ComplexTextShaperUtil
     * @brief 복합 문자 셰이퍼를 만듭니다.
     */
    struct ComplexTextShaperUtil
    {
        /** @brief 셰이퍼를 만듭니다. 런마다 같은 폰트 바이트(`IFontRasterizer::findFaceBytes`)로 자기 면을 만들어 캐시합니다(게임 스레드). */
        static unique_ptr<ITextShaper> create();
    };
} // namespace sw
