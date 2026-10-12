/**
 * @file RenderTargetDeclaration.h
 * @brief 렌더 타깃 텍스처의 크기를 텍스처 캐시보다 먼저 알리는 창구입니다(`engine::declareRenderTarget`).
 * @details 컴포넌트 모델 코어(`CameraComponent`)는 Graphics 폴더를 include 하지 않는다(CheckEngineLayers). 그래서 카메라는 이 함수로 크기를 알리고,
 *          텍스처 캐시(`TextureCache`)가 만들어질 때 실제 처리 함수를 묶는다. 묶인 함수가 없으면(텍스처 캐시를 만든 적이 없다) 아무것도 하지 않는다.
 *          프레임마다 부르는 경로가 아니다 — 카메라 등록과 출력 변경 때만 부른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    namespace engine
    {
        /** @brief 렌더 타깃 경로와 크기(픽셀)를 받는 처리 함수입니다. */
        using RenderTargetDeclareFunction = void ( * )( string_view relativePath, uint32 width, uint32 height );

        /** @brief 처리 함수를 묶습니다. 텍스처 캐시가 만들어질 때 부릅니다(같은 함수를 다시 묶어도 된다). */
        SW_API void bindRenderTargetDeclaration( RenderTargetDeclareFunction function );

        /** @brief 렌더 타깃 텍스처의 크기를 알립니다. 묶인 처리 함수가 없으면 아무것도 하지 않습니다. */
        SW_API void declareRenderTarget( string_view relativePath, uint32 width, uint32 height );
    } // namespace engine
} // namespace sw
