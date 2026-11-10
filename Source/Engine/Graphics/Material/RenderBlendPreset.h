/**
 * @file RenderBlendPreset.h
 * @brief 데이터로 둔 블렌드 프리셋 표(`engine/renderstate/blendpresets.xml`)입니다. 파이프라인 XML 과 머티리얼 XML 이 이름으로 고릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 이름 붙은 블렌드 타깃 값 하나입니다. */
    REFLECT()
    struct SW_API RenderBlendPreset : public RHIBlendTargetState
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name;
    };

    /**
     * @struct RenderBlendPresetTable
     * @brief 프리셋 표 파일 하나입니다. 첫 조회 때 한 번 읽고 프로세스가 끝날 때까지 둡니다(언리얼의 정적 블렌드 상태 · 유니티 쉐이더랩의 Blend 표기를 파일 한 벌로 모은 자리).
     */
    REFLECT()
    struct SW_API RenderBlendPresetTable
    {
        REFLECT_BODY();

        PROPERTY()
        vector<RenderBlendPreset> _listPreset;

        /** @brief 프리셋 파일의 리소스 경로입니다. */
        static constexpr const utf8* kAssetPath = "engine/renderstate/blendpresets.xml";

        /**
         * @brief 이름으로 프리셋을 찾습니다. 여러 스레드에서 불러도 됩니다(머티리얼은 워커가 읽는다).
         * @return 없으면 nullptr 입니다. 부르는 쪽이 읽기 오류로 알린다 — 불투명으로 대신하지 않는다. 표를 읽지 못했으면 그 이유를 로그에 한 번 남기고 모두 nullptr 입니다.
         */
        [[nodiscard]] static const RenderBlendPreset* find( string_view name );
    };
} // namespace sw
