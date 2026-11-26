/**
 * @file Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h
 * @brief 쿠킹 전에 셰이더 소스를 만드는 생성기의 창구입니다(머티리얼 그래프 컴파일러 등). 개발 코드가 정적 등록하고, 셰이더 쿠커는 이 창구만 부른다.
 * @details 엔진이 개발 코드(그래프 컴파일러)를 직접 부르지 않게 하는 자리다 — 유니티 Shader Graph 가 `ScriptedImporter` 로 임포트 파이프라인에 스스로 붙는 모양(설계
 *          `docs/agents/Rendering.md` "머티리얼 그래프"). 개발 구성에서 등록이 0 이면 쿠킹 실패다 — 낡은 생성물로 굽지 않는다. 배포 구성은 생성기가 없다(굽기 전 생성물이 이미 맞다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    class AssetPaths;

    /** @class IShaderSourceGenerator @brief 쿠킹 전 셰이더 소스 생성기 하나입니다. 상태가 없다(const) — 헤드리스 작업 스레드에서 부릅니다. */
    class SW_API IShaderSourceGenerator
    {
    public:
        virtual ~IShaderSourceGenerator() = default;

        /** @brief 생성기 이름입니다(등록 표의 이름 · 순서 · 로그). */
        [[nodiscard]] virtual const utf8* getName() const = 0;
        /** @brief @p contentRoot 아래 생성물을 소스 트리에 맞춥니다(바이트가 같으면 쓰지 않는다). 하나라도 실패하면 false — 쿠킹은 실패로 센다. 쓴 파일 수를 @p outWrittenCount 에 더한다. */
        [[nodiscard]] virtual bool refreshAll( const AssetPaths& assetPaths, const AbsolutePath& contentRoot, uint32& outWrittenCount ) const = 0;

        /** @brief 생성기를 겁니다(정적 등록자가 부른다). 같은 이름이 이미 있으면 공용 거절 로그를 남기고 false 입니다. */
        [[nodiscard]] static bool add( const IShaderSourceGenerator* pGenerator );
        /** @brief 건 생성기를 뗍니다(건 이미지가 내려갈 때). */
        static void remove( const IShaderSourceGenerator* pGenerator );

        /**
         * @brief 등록된 생성기를 이름 순으로 모두 돕니다(`refreshGenerators`). 개발 구성에서 등록이 0 이면 `ShaderCooker.NoSourceGenerator` 오류로 false 입니다.
         * @details 배포 구성은 생성기를 짓지 않는다 — 등록이 0 이어도 할 일이 없어 true 다.
         */
        [[nodiscard]] static bool refreshAllRegistered( const AssetPaths& assetPaths, const AbsolutePath& contentRoot, uint32& outWrittenCount );
        /**
         * @brief @p listGenerator 를 차례로 돕니다. 비었거나(@p bRequireGenerator 일 때) 하나라도 실패하면 오류를 남기고 false 입니다(나머지는 계속 돈다).
         * @details 등록 표와 떼어 둔 본체다 — 테스트가 빈 목록 · 실패하는 생성기를 그대로 넘긴다.
         */
        [[nodiscard]] static bool refreshGenerators( const vector<const IShaderSourceGenerator*>& listGenerator, bool bRequireGenerator, const AssetPaths& assetPaths,
                                                     const AbsolutePath& contentRoot, uint32& outWrittenCount );

        /** @struct Registrar @brief 생성기를 정적 객체로 겁니다 — 공통 틀 `StaticRegistrar` 라 건 이미지가 내려가면 뗀다. */
        struct Registrar final : StaticRegistrar<const IShaderSourceGenerator*>
        {
            explicit Registrar( const IShaderSourceGenerator& generator )
                : StaticRegistrar{ &generator, &IShaderSourceGenerator::add, &IShaderSourceGenerator::remove }
            {
            }
        };
    };
} // namespace sw
