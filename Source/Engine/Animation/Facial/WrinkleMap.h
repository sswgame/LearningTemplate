/**
 * @file WrinkleMap.h
 * @brief 주름 맵 매핑입니다. 표정 가중치를 얼굴 영역(마스크 채널)마다의 주름 가중치로 바꾸고, 셰이더(`material_feature_wrinkle.hlsli`)가 그 가중치로 주름 노멀(과 선택으로 알베도 · 캐비티)을 섞습니다.
 * @details 영역은 마스크 텍스처 두 장의 RGBA 채널(번호 0..7)이다 — 이마 · 미간 · 눈가 · 콧등 · 팔자 · 입가 같은 나눔은 마스크를 그린 사람이 정하고, 어느 표정이 어느 영역을 얼마나
 *          주름지게 하는지는 데이터(캐릭터 몸 프로필의 `<Wrinkle>`)가 정한다. 언리얼 MetaHuman 의 애니메이션 주름 맵(표정 커브 → 마스크 가중치 → 노멀 블렌드)과 같은 길이다.
 *          가중치 = saturate( 강도 × Σ 출처 가중치 × 표정 가중치 ). 표정 이름은 얼굴 리그(`.facial.xml`)의 표정이다 — 애니메이터의 같은 이름 커브와 실시간 입력이 이미 더해진 값이다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 주름 영역 하나에 표정 하나가 주는 몫입니다(`<Source expression weight/>`). */
    REFLECT()
    struct SW_API WrinkleSourceDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Facial rig expression name (its weight includes the animator curve of the same name)" )
        hashed_string _expression{};
        PROPERTY( Tooltip = "Region weight at expression weight 1" )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 주름 영역 하나입니다(`<Wrinkle region mask>` 아래 `<Source>`). `_mask` 는 마스크 채널 번호(0..7 — 마스크 A 의 RGBA, 마스크 B 의 RGBA)입니다. */
    REFLECT()
    struct SW_API WrinkleRegionDef
    {
        REFLECT_BODY();

        vector<WrinkleSourceDef> _listSource{};
        PROPERTY( Required, Tooltip = "Region name for the editor (Forehead, Brow, EyeCorner, Nose, Smile, Mouth)" )
        hashed_string _region{};
        PROPERTY( Required, Min = 0, Max = 7, Tooltip = "Mask channel: 0..3 = mask A RGBA, 4..7 = mask B RGBA" )
        uint32 _mask{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 마스크 채널 여덟 개의 주름 가중치입니다(셰이더 매개변수 두 개). */
    struct WrinkleWeights
    {
        float4 _maskA{}; ///< 채널 0..3
        float4 _maskB{}; ///< 채널 4..7

        bool isZero() const { return _maskA == float4{} && _maskB == float4{}; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class WrinkleMap
     * @brief 파일 머리말 참고. 영역 목록과 강도를 들고 표정 가중치를 마스크 가중치로 바꿉니다(순수 계산 — 테스트가 직접 부른다).
     */
    class SW_API WrinkleMap
    {
    public:
        /** @brief 마스크 채널 수입니다(마스크 텍스처 두 장 × RGBA). */
        static constexpr uint32 kMaskChannelCount = 8;
        /** @brief 셰이더 매개변수 이름입니다(`material_feature_wrinkle.hlsli` 의 머티리얼 칸과 같은 글 — 셰이더와 코드가 이름으로 묶인다). */
        static constexpr const utf8* kMaskAParameter = "_wrinkleWeightsA";
        static constexpr const utf8* kMaskBParameter = "_wrinkleWeightsB";

        WrinkleMap();
        WrinkleMap( vector<WrinkleRegionDef> listRegion, float32 intensity );

        const vector<WrinkleRegionDef>& getRegions() const { return _listRegion; }
        float32                         getIntensity() const { return _intensity; }
        bool                            isEmpty() const { return _listRegion.empty(); }

        /**
         * @brief 표정 가중치(@p listExpressionName 과 같은 순서의 @p listExpressionWeight)로 마스크 가중치를 구합니다. 모르는 표정 이름의 출처는 0 이다(로드 검사가 막는다).
         * @details 같은 마스크 채널을 둘 이상의 영역이 쓰면 더한 뒤 자른다.
         */
        void evaluate( span<const hashed_string> listExpressionName, span<const float32> listExpressionWeight, WrinkleWeights& outWeights ) const;
        /** @brief 출처의 표정 이름이 모두 @p listExpressionName 에 있는지 봅니다. 없는 이름은 @p pOutMissing 에 담고 false 입니다. */
        [[nodiscard]] bool checkExpressions( span<const hashed_string> listExpressionName, vector<hashed_string>* pOutMissing ) const;

    private:
        vector<WrinkleRegionDef> _listRegion;
        float32                  _intensity;
    };
} // namespace sw
