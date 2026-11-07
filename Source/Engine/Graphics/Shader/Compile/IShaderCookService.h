/**
 * @file Engine/Graphics/Shader/Compile/IShaderCookService.h
 * @brief 셰이더 쿠킹(모든 셰이더 굽기 · 쿠킹본 판정)을 렌더러에 맡기는 창구입니다 — 헤드리스 작업(엔진 루프 쪽)은 렌더러 헤더를 읽지 않습니다.
 * @details 구현은 렌더러(`Engine/Renderer/Cook/ShaderCookService.cpp`)의 정적 등록 하나다. 전용 서버 구성은 렌더러를 짓지 않아 창구가 없다 — 서버 팩에는 셰이더 바이너리가 없고,
 *          `--cook-shaders` · `--verify-shaders` 는 셰이더 쿠커가 없다는 오류로 멈춘다. 예전에는 `HeadlessSubsystem.cpp` 의 `#if SW_WITH_CLIENT_CODE` 구역이었다
 *          (`IEngineRenderDriver` 와 같은 갈림 — 언리얼 쿠커가 셰이더 형식 모듈 `IShaderFormat` 을 인터페이스로 찾는 것과 같다, 기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /** @class IShaderCookService @brief 셰이더 쿠킹을 맡는 쪽입니다. 헤드리스 작업 스레드에서 부릅니다. */
    class SW_API IShaderCookService
    {
    public:
        virtual ~IShaderCookService() = default;

        /** @brief 셰이더를 @p cookedDir 에 굽습니다(바뀐 것만). 모두 깨끗하면 true 입니다. 비면 쿠커가 오류로 멈춘다. */
        [[nodiscard]] virtual bool cookAllShaders( string_view cookedDir ) = 0;
        /** @brief @p cookedDir 의 @p format 셰이더 바이너리가 소스와 맞는지 보고 문제를 오류로 남깁니다. 맞으면 true 입니다. */
        [[nodiscard]] virtual bool verifyCookedShaders( string_view cookedDir, ShaderTargetFormat format ) = 0;

        /** @brief 등록된 창구입니다. 렌더러를 짓지 않는 구성(전용 서버)은 nullptr 입니다. */
        [[nodiscard]] static IShaderCookService* find();

        /** @struct Registration @brief 렌더러가 정적 객체로 창구를 겁니다(이미지에 하나). */
        struct SW_API Registration
        {
            explicit Registration( IShaderCookService& service );
        };
    };
} // namespace sw
