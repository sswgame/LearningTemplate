/**
 * @file AppearancePreload.h
 * @brief 외형 프리셋이 읽는 에셋 경로를 모읍니다 — 전환 앞에 미리 올릴 묶음(`PreloadSet`)에 넘긴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;

    /**
     * @struct AppearancePreloadUtil
     * @brief 프리셋 참조를 따라 에셋 경로를 모읍니다(전부 static).
     */
    struct SW_GF_API AppearancePreloadUtil
    {
        /**
         * @brief 프리셋을 @p seed 로 펼쳐 해석한 외형(장비 없이 프리셋 그대로)이 읽는 에셋 경로를 @p inoutListPath 에 덧붙입니다. 이미 있는 경로는 다시 넣지 않습니다.
         * @details 부품의 메시 · 프리팹 · 스프라이트, 머티리얼, 스켈레톤, 소켓 에셋, 피팅 표 · 피팅 데이터와 부착물의 에셋이다. 같은 프리셋도 시드가 다르면 고른 후보가 다르므로
         *          미리 올릴 시드마다 부른다.
         * @return 프리셋을 펼치지 못하면(모르는 id) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool collectPresetAssets( const AppearanceDatabase& database, const hashed_string& presetID, uint32 seed, vector<string>& inoutListPath );
    };
} // namespace sw
