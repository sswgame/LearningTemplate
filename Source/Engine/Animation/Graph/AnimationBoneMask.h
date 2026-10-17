/**
 * @file AnimationBoneMask.h
 * @brief 애니메이션 레이어가 쓰는 본 마스크 데이터입니다(언리얼 Layered Blend Per Bone 의 Branch Filter, 유니티 Avatar Mask 에 해당합니다).
 * @details JSON 형식은 다음과 같다. 그래프 에셋의 "bone_masks" 항목에 바로 적거나, 같은 객체를 `*.bonemask.json` 프리셋 파일에 두고 "file" 로 가리킨다.
 *          @code
 *          { "roots": [ { "bone": "Spine", "weight": 1.0 } ], "bones": [ { "bone": "Spine", "weight": 0.5 } ] }
 *          @endcode
 *          - **roots**: 그 본과 모든 자손에 가중치를 퍼뜨린다. 자손 쪽에 다른 root 가 있으면 그 아래는 그 root 의 값이다.
 *          - **bones**: 그 본 하나만 가중치를 바꾼다(퍼뜨리지 않는다). 퍼뜨린 뒤에 적용한다.
 *          어느 쪽에도 닿지 않는 본은 0 이다. 가중치는 0 이상 1 이하이고 필수다. 스켈레톤에 없는 본 이름은 오류다(조용히 건너뛰지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class JSONValue;
    class Skeleton;

    /** @brief 본 마스크의 항목 하나입니다. */
    struct AnimationBoneMaskEntry
    {
        hashed_string _bone{};
        float32       _weight{ 0.0f };
        uint8         _bPropagate{ SW_TRUE }; ///< "roots" 면 자손에 퍼뜨리고, "bones" 면 그 본 하나다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationBoneMaskDef
     * @brief 이름 붙은 본 마스크 하나입니다. 파일 머리말의 형식을 읽고, 스켈레톤에 맞춰 본마다의 가중치를 구합니다.
     */
    struct SW_API AnimationBoneMaskDef
    {
        /** @brief 본 마스크 프리셋 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".bonemask.json";

        hashed_string                  _name{};
        string                         _filePath{}; ///< 프리셋 파일에서 읽었으면 그 경로입니다. 그래프를 쓸 때 본문 대신 이 경로를 씁니다.
        vector<AnimationBoneMaskEntry> _listEntry{};

        /**
         * @brief "roots" 와 "bones" 를 읽습니다(지금 항목을 비우고). 항목이 하나도 없거나, 본 이름이 비었거나, 가중치가 없거나 범위 밖이면 오류를 남기고 false 입니다.
         * @param context 오류 문장에 넣을 위치(그래프 파일과 마스크 이름)입니다.
         */
        [[nodiscard]] bool parseObject( const JSONValue& object, string_view context );
        /** @brief 프리셋 파일을 읽습니다. 성공하면 `_filePath` 를 그 경로로 둡니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief "roots" 와 "bones" 를 @p object(이미 객체여야 합니다)에 씁니다. */
        void writeObject( const JSONValue& object ) const;
        /**
         * @brief 스켈레톤의 본마다 가중치를 구해 @p outListWeight(본 수 크기)에 씁니다.
         * @return 스켈레톤에 없는 본을 가리키면 오류를 남기고 false 입니다. 그때 @p outListWeight 는 모두 0 입니다.
         */
        [[nodiscard]] bool computeBoneWeights( const Skeleton& skeleton, vector<float32>& outListWeight ) const;
    };
} // namespace sw
