/**
 * @file Engine/Graphics/Shader/Compile/IShaderCookService.h
 * @brief 셰이더 쿠킹(모든 셰이더 굽기 · 쿠킹본 판정)을 렌더러에 맡기는 창구입니다 — 헤드리스 작업(엔진 루프 쪽)은 렌더러 헤더를 읽지 않습니다.
 * @details 구현은 렌더러(`Engine/Renderer/Cook/ShaderCookService.cpp`)의 정적 등록 하나다. 전용 서버 구성은 렌더러를 짓지 않아 창구가 없다 — 서버 팩에는 셰이더 바이너리가 없고,
 *          `--cook-shaders` · `--verify-shaders` 는 셰이더 쿠커가 없다는 오류로 멈춘다. 예전에는 `HeadlessSubsystem.cpp` 의 `#if SW_WITH_CLIENT_CODE` 구역이었다
 *          (`IEngineRenderDriver` 와 같은 갈림 — 언리얼 쿠커가 셰이더 형식 모듈 `IShaderFormat` 을 인터페이스로 찾는 것과 같다, 기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    enum class ShaderTargetFormat : uint8;

    /** @class IShaderCookService @brief 셰이더 쿠킹을 맡는 쪽입니다. 헤드리스 작업 스레드에서 부릅니다. */
    class SW_API IShaderCookService
    {
    public:
        virtual ~IShaderCookService() = default;

        /**
         * @brief 셰이더를 @p cookedDir 에 @p format 바이너리로만 굽습니다(바뀐 것만). 모두 깨끗하면 true 입니다. 비면 쿠커가 오류로 멈춘다.
         * @details 팩은 형식 하나만 담는다(`cookPackStage`) — 네 형식을 다 구우면 팩에 들지 않는 셋에 쿠킹 시간을 쓴다(교차 쿠킹이 3 시간 걸렸다). 스탬프는 형식 폴더마다 따로다.
         */
        [[nodiscard]] virtual bool cookAllShaders( string_view cookedDir, ShaderTargetFormat format ) = 0;
        /** @brief @p cookedDir 의 @p format 셰이더 바이너리가 소스와 맞는지 보고 문제를 오류로 남깁니다. 맞으면 true 입니다. */
        [[nodiscard]] virtual bool verifyCookedShaders( string_view cookedDir, ShaderTargetFormat format ) = 0;
        /**
         * @brief 쿠킹 요청이 @p format 으로 만드는 바이너리의 스테이징 기준 id(`engine/shaders/bin/dx12/<파일>`, 정규화)를 모읍니다. 요청을 모으지 못하면 false 입니다.
         * @details 팩 쿠커가 요청했는데 원본이 닿지 않아 버리는 바이너리를 경고로 가른다 — 런타임이 찾을 셰이더가 팩에 없다(첫 배포 굽기의 layer_bake).
         */
        [[nodiscard]] virtual bool collectRequestedBinaries( ShaderTargetFormat format, vector<string>& outListBinaryID ) = 0;

        /** @brief 등록된 창구입니다. 렌더러를 짓지 않는 구성(전용 서버)은 nullptr 입니다. */
        [[nodiscard]] static IShaderCookService* find();

        /** @brief 창구를 겁니다(정적 등록자가 부른다). 이미 다른 창구가 걸려 있으면 공용 거절 로그를 남기고 false 입니다 — 이미지에 하나다. */
        [[nodiscard]] static bool attach( IShaderCookService* pService );
        /** @brief 걸린 창구를 뗍니다(건 이미지가 내려갈 때). 걸려 있지 않으면 할 일이 없다. */
        static void detach( IShaderCookService* pService );

        /** @struct Registration @brief 렌더러가 정적 객체로 창구를 겁니다(이미지에 하나) — 공통 틀 `StaticRegistrar` 라 건 이미지가 내려가면 뗀다. */
        struct Registration final : StaticRegistrar<IShaderCookService*>
        {
            explicit Registration( IShaderCookService& service )
                : StaticRegistrar{ &service, &IShaderCookService::attach, &IShaderCookService::detach }
            {
            }
        };
    };
} // namespace sw
