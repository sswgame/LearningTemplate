/**
 * @file OcclusionCullingConfig.h
 * @brief GPU 컬링의 HZB 가림 판정 설정입니다. 값은 `Config/Engine/OcclusionCulling.json` 에 있고 이 헤더는 모양만 둡니다.
 * @details 설정 층(1)에 두어 설정 파일 검사(`ConfigFileSchemaTest`)가 렌더러 층 없이 읽는다. 가림 판정을 켜고 끄는 것은 실행 인자 `-gv_occlusionCulling`(프레임 설정)이고,
 *          여기는 켜졌을 때의 모양(2 단계 · 문턱)이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/ConfigFileID.h"
#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct OcclusionCullingConfig
     * @brief HZB 가림 판정의 설정입니다(`Config/Engine/OcclusionCulling.json` — 모든 칸이 필수). 맞지 않으면 `validate` 가 이유를 돌려주고 렌더러는 오류로 멈춘다(기본값으로 대신하지 않는다).
     */
    REFLECT()
    struct SW_API OcclusionCullingConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 설정 파일의 위치입니다(프로젝트 폴더 기준). */
        static constexpr ConfigFileID<OcclusionCullingConfig> kConfigFile{ "Config/Engine/OcclusionCulling.json" };
        /// 판정이 한 축에 읽는 HZB 텍셀 상한의 최댓값입니다 — 스레드 하나가 최대 4 x 4 = 16 번 읽는다(gpu_cull.hlsl `kMaxHZBFootprint`).
        static constexpr uint32 kMaxFootprintTexels = 4;

        PROPERTY( Required, Tooltip = "Two-phase occlusion: re-test the instances the last-frame HZB rejected against an HZB built from this frame's depth prepass, and draw the ones now visible" )
        bool _bTwoPhase{};
        PROPERTY( Required, Tooltip = "HZB texels read per axis by one occlusion test, in [1, 4] - the mip is picked so the screen rect spans at most this many texels (2 reads 2 x 2)" )
        uint32 _footprintTexels{ 0 };
        PROPERTY( Required, Tooltip = "NDC depth bias, >= 0 - an instance is occluded only when its nearest depth is farther than the HZB max depth plus this" )
        float32 _depthBias{ -1.0f };

        /** @brief 칸의 범위를 봅니다(`ConfigManager::readConfig` 가 부른다). */
        [[nodiscard]] expected<void> validate() const
        {
            if ( _footprintTexels < 1u || _footprintTexels > kMaxFootprintTexels )
                return unexpected( Error{ ErrorKind::InvalidArgument, "_footprintTexels must be in [1, 4]", kConfigFile.getPath() } );
            if ( _depthBias < 0.0f )
                return unexpected( Error{ ErrorKind::InvalidArgument, "_depthBias must be >= 0", kConfigFile.getPath() } );
            return {};
        }
    };
} // namespace sw
