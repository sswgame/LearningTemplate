/**
 * @file CharacterMirrorUtil.h
 * @brief 좌우 대칭 편집 도우미입니다. 이름 규칙 표로 반대편 이름을 찾고, 위치 · 회전을 대칭 축 기준으로 뒤집습니다.
 * @details 짝은 **이름 규칙으로만** 찾는다(`_L`/`_R`, `Left`/`Right` 등). 위치로 짝을 찾으면 비대칭 리그에서 틀린다(설계 문서 "결정과 위험").
 *          토큰은 이름의 앞이나 뒤에 붙었거나, 양쪽이 구분자(`_` `.` `-` 공백)나 이름 끝일 때만 짝으로 본다 — `Shoulder_Pad` 의 `r_` 을 우측 표시로 읽지 않기 위해서다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetID.h"

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
        /** @brief 기본 표 파일입니다(`_L`/`_R`, `.L`/`.R`, `Left`/`Right` 등 흔한 철자). 철자를 더하는 일이 줄 하나다. 에디터만 읽으므로 에디터 팩에 둔다. */
        static constexpr string_view kDefaultPath = "editor/character/default.mirrornames.xml";

        /**
         * @brief `<MirrorNames><Pair first="_L" second="_R"/></MirrorNames>` 형식을 읽어 현재 표를 바꿉니다. 모르는 원소 · 속성, 빈 토큰, 같은 두 토큰은 오류입니다.
         * @details 실패하면 현재 표를 그대로 둡니다. 쌍의 순서가 맞춰 보는 순서입니다.
         */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 현재 표를 XML 텍스트로 씁니다. */
        string saveToXMLText() const;

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
