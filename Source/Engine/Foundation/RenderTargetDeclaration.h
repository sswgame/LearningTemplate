/**
 * @file RenderTargetDeclaration.h
 * @brief 렌더 타깃 텍스처의 크기를 텍스처 캐시보다 먼저 받는 쪽의 인터페이스입니다(`IRenderTargetDeclarationSink`).
 * @details 컴포넌트 모델 코어(`CameraComponent`)는 Graphics 폴더를 include 하지 않는다. 그래서 카메라는 엔진 범위에서 이 인터페이스를 찾아
 *          (`engine::findSubsystem<IRenderTargetDeclarationSink>()`) 크기를 알린다. 구현은 텍스처 캐시(`TextureCache`)이고, 에셋 매니저가 소유한 그 캐시를
 *          엔진 범위에 이 타입으로 건다(`EngineSubsystemSet`) — 함수 칸을 두고 위 층이 채우지 않는다(언리얼 `IModularFeatures` 의 구현 등록과 같은 자리).
 *          범위에 없으면(텍스처 캐시를 걸지 않은 테스트 하네스) 알릴 곳이 없다. 프레임마다 부르는 경로가 아니다 — 카메라 등록과 출력 변경 때만 부른다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class IRenderTargetDeclarationSink
     * @brief 렌더 타깃 경로와 크기(픽셀)를 텍스처를 만들기 전에 받습니다. 게임 스레드에서 부릅니다.
     */
    class IRenderTargetDeclarationSink
    {
    public:
        virtual ~IRenderTargetDeclarationSink() = default;

        /** @brief 렌더 텍스처 @p relativePath 의 크기를 알립니다. 이미 만든 것은 바꾸지 않는다(다음에 새로 만들 때부터). */
        virtual void declareRenderTarget( string_view relativePath, uint32 width, uint32 height ) = 0;
    };
} // namespace sw
