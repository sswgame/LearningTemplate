/**
 * @file CharacterMirrorUtil.h
 * @brief 좌우 대칭 편집 도우미입니다. 이름 규칙 표로 반대편 이름을 찾고, 위치 · 회전을 대칭 축 기준으로 뒤집습니다.
 * @details 짝은 **이름 규칙으로만** 찾는다(`_L`/`_R`, `Left`/`Right` 등). 위치로 짝을 찾으면 비대칭 리그에서 틀린다(설계 문서 "결정과 위험").
 *          토큰은 이름의 앞이나 뒤에 붙었거나, 양쪽이 구분자(`_` `.` `-` 공백)나 이름 끝일 때만 짝으로 본다 — `Shoulder_Pad` 의 `r_` 을 우측 표시로 읽지 않기 위해서다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 대칭 평면의 법선 축입니다. 캐릭터는 보통 X 축(왼쪽 · 오른쪽)입니다. */
    enum class MirrorAxis : uint8
    {
        X = 0,
        Y,
        Z,
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 좌우 이름 토큰 한 쌍입니다(예: `_L` 과 `_R`). */
    struct MirrorTokenPair
    {
        string _first{};
        string _second{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MirrorNameRules
     * @brief 반대편 이름을 찾는 규칙 표입니다. 앞에 있는 쌍이 먼저 맞춰 봅니다.
     */
    class MirrorNameRules
    {
    public:
        /** @brief 흔한 철자의 기본 표입니다(`_L`/`_R`, `.L`/`.R`, `_l`/`_r`, `Left`/`Right`, `left`/`right`, `L_`/`R_`, `l_`/`r_`). */
        static MirrorNameRules makeDefault();

        /** @brief 쌍을 끝에 더합니다. 어느 쪽이든 비어 있거나 두 토큰이 같으면 무시하고 false 입니다. */
        [[nodiscard]] bool addPair( string_view first, string_view second );
        /**
         * @brief @p name 의 반대편 이름을 구합니다. 짝 토큰이 없는 이름(가운데 본)이면 false 이며 @p outMirrorName 은 바뀌지 않습니다.
         * @details 토큰은 대소문자를 구분한다(`_L` 은 `_l` 과 다른 쌍이다).
         */
        [[nodiscard]] bool findMirrorName( string_view name, string& outMirrorName ) const;
        /** @brief 가운데 이름(짝이 없는 이름)이면 true 입니다. */
        bool isCenterName( string_view name ) const;

        const vector<MirrorTokenPair>& getPairs() const { return _listPair; }

    private:
        vector<MirrorTokenPair> _listPair{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 위치 · 회전을 대칭 평면 기준으로 뒤집는 함수 모음입니다(전부 static). */
    struct CharacterMirrorUtil
    {
        /** @brief 위치를 평면 법선 성분만 부호를 뒤집어 반환합니다. */
        static float3 mirrorTranslation( const float3& translation, MirrorAxis axis );
        /**
         * @brief 회전을 평면 기준으로 뒤집습니다. 반사는 회전축을 의사벡터로 바꾸므로, 법선 축 성분만 두고 나머지 허수부 성분의 부호를 뒤집습니다.
         */
        static quaternion mirrorRotation( const quaternion& rotation, MirrorAxis axis );
    };
} // namespace sw::editor
