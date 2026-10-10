/**
 * @file VertexAnimationCooker.h
 * @brief 정점 애니메이션(VAT) 쿠킹 — 데이터(`*.vertexanimation.json`)가 고른 (메시, 클립)을 쿠킹 때 `.vat` 로 굽습니다.
 * @details 배포본은 쿠킹본을 팩에서 읽고(`AnimationCrowd::findVertexAnimationMesh`), Dev 는 쿠킹 폴더를 마운트하지 않아 처음 쓸 때 같은 함수
 *          (`MeshVertexAnimationBaker`)로 굽습니다 — 두 길의 결과가 바이트까지 같습니다. 쿠킹은 `App --cook-scenes` 단계가 부릅니다.
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

    /**
     * @struct VertexAnimationCookList
     * @brief 쿠킹 목록 하나(`<이름>.vertexanimation.json`): `{ "mesh", "skeleton", "clip_folder", "clips": [ 클립 이름 ... ] }`.
     * @details 모르는 키 · 없는 메시 · 스켈레톤 · 클립 파일은 오류입니다(`ResourceDataSchemaTest` 가 확인합니다). 루트 모션은 묶지 않고 굽습니다
     *          (루트 모션을 뽑는 유닛은 쿠킹본을 쓰지 않고 처음 쓸 때 굽는다).
     */
    struct SW_API VertexAnimationCookList
    {
        /** @brief 목록 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".vertexanimation.json";

        string                _meshPath;
        string                _skeletonPath;
        string                _clipFolder;
        vector<hashed_string> _listClip;

        /** @brief JSON 을 읽습니다(가리키는 파일이 있는지도 봅니다). 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool parseJSON( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 클립 이름의 파일 경로입니다(`<clip_folder>/<소문자>.animclip` — 애니메이터와 같은 규칙). */
        string makeClipPath( const hashed_string& clipName ) const;

    private:
        [[nodiscard]] bool parseRoot( const JSONValue& root, string_view sourceLabel );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VertexAnimationCooker
     * @brief `Resource/` 아래 쿠킹 목록을 모두 찾아 `.vat` 를 `<cookedDir>/<메시 경로에서 만든 이름>` 에 씁니다.
     */
    struct SW_API VertexAnimationCooker
    {
        /**
         * @brief 모든 목록을 굽습니다. 활성 게임이 아닌 게임 팩의 목록은 건너뜁니다(배포본은 활성 게임의 팩만 연다).
         * @param framesPerSecond VAT 프레임율(군중 표 `animationcrowd.json` 과 같은 값이어야 런타임 굽기와 같다).
         * @return 쓴 `.vat` 수입니다. 읽지 못한 목록 · 굽지 못한 클립은 @p outFailedCount 에 셉니다.
         */
        static uint32 cookAll( const string& resourceRoot, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount );
        /** @brief 목록 하나를 굽습니다. 쓴 수를 돌려줍니다. @p cookedDir 가 비었으면 메시 경로 곁에 씁니다(시험). */
        static uint32 cookList( const VertexAnimationCookList& list, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount );
    };
} // namespace sw
