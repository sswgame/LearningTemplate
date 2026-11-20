/**
 * @file MaterialPermutation.h
 * @brief 머티리얼 퍼뮤테이션 define 목록(용도 · 품질 · 키워드)과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw
{
    /** @brief 머티리얼 퍼뮤테이션 define 목록(용도 · 품질 · 키워드)과 그 해시, 퍼뮤테이션이 바뀐 세대를 담당합니다. */
    struct SW_API MaterialPermutation
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 증가하는 세대입니다. 엔진에 묶인 머티리얼 캐시의 변경 기록이다(`MaterialCache::getPermutationLog`) — 엔진이 없으면 0(모름)입니다.
         * @details **정지한 씬을 위한 신호입니다.** `GPUScene` 은 "등록과 해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뜁니다. 그런데 머티리얼과 인스턴스의 정적 스위치, 키워드, 멀티 컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          **바뀐 퍼뮤테이션이 영원히 반영되지 않습니다**(움직이는 씬에서는 가려집니다).
         *          세대와 함께 바꾼 주인(머티리얼 · 인스턴스)을 적어, 읽는 쪽은 그 주인을 쓰는 프리미티브만 다시 모은다(키워드 하나에 장면 전체를 다시 모으지 않는다).
         */
        static uint64 getPermutationGeneration();

        /**
         * @brief 세대를 올리고 @p pOwner(define 이 바뀐 머티리얼 · 인스턴스)를 적습니다. define 이 실제로 달라진 설정에서만 부릅니다.
         * @param pOwner 머티리얼이면 그 머티리얼(과 그 인스턴스)을 쓰는 프리미티브, 인스턴스면 그 인스턴스를 쓰는 프리미티브가 다시 모인다. nullptr 이면 모두다.
         */
        static void recordPermutationChange( const void* pOwner );

        static void appendUniqueDefine( vector<string>& outListDefine, string_view define );

        /**
         * @brief 변형 define 을 바탕 목록에 얹습니다 — 같은 이름(`=` 앞)이 있으면 그 자리의 값을 바꾸고, 없으면 뒤에 붙입니다.
         * @details 패스 PSO 는 머티리얼 없는 배치를 위해 `MATERIAL_TEXTURE_COUNT=0` 을 갖고, 머티리얼 변형은 같은 이름을 자기 칸 수로 바꾼다 — 같은 이름이 둘이면
         *          셰이더 컴파일이 재정의로 갈린다. 런타임 PSO 변형(FramePSOManager)과 쿠커(ShaderCookRequestCollector)가 이 함수 하나로 합쳐 같은 목록 · 같은 차례가 된다.
         */
        static void mergeDefine( vector<string>& inoutListDefine, string_view define );

        /** @brief `MATERIAL_TEXTURE_COUNT=<count>` define 입니다(`kMaterialTextureCountDefine`). */
        static string makeTextureCountDefine( uint32 count );

        static void appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );

        static void appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );
        /**
         * @brief 표면 모델의 define `MATERIAL_SURFACE_MODEL_<이름>` 을, 포워드 전용 모델이면 `MATERIAL_FORWARD_ONLY` 도 더합니다.
         * @details 셰이더 뼈대(material_shader.hlsli)가 모델 define 과 조명 훅을 맞춰 보고, G버퍼 패스는 포워드 전용 배치를 뺀다(`RenderPassKindInfo` G버퍼 줄).
         */
        static void appendSurfaceModelDefines( MaterialSurfaceModel model, vector<string>& outListDefine );
        /** @brief G버퍼에 칸이 없어 포워드로만 그리는 표면 모델이면 true 입니다(`Toon` · `Hair` · `CustomLit`). */
        [[nodiscard]] static bool isForwardOnlySurfaceModel( MaterialSurfaceModel model );

        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
