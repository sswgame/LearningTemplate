/**
 * @file LocalizationCooker.h
 * @brief 로컬라이제이션 프로젝트마다 원문 문화권과 출시 문화권(`stagedCultures`)의 이진 번역 표(`.locres`)를 쿠킹 스테이징에 굽습니다(감사 A-16).
 * @details 배포본(쿠킹본을 읽는 구성)은 저작 파일(원문 표 · 번역 표 XML)을 파싱하지 않고 조회 사슬에 든 문화권의 표만 읽는다(`LocalizationManager::mountProject`).
 *          언리얼 `.locres` 와 같은 자리다 — 낡은 · 검토 번역은 쿠킹이 미리 뺀다. 출력 이름은 `LocalizationProject::makeCookedTablePath` 하나가 정한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/CookJob.h"

namespace sw
{
    class CookStore;
} // namespace sw

namespace sw
{
    /**
     * @struct LocalizationCooker
     * @brief 이진 번역 표 쿠킹입니다. 헤드리스 쿠킹의 씬 단계(`--cook-scenes` · `--cook-all`)가 다른 쿠커와 함께 부른다.
     */
    struct SW_API LocalizationCooker
    {
        /**
         * @brief @p sourceRoot 아래 프로젝트(`*.locproject.xml`)마다 쿠킹된 문화권(`LocalizationProject::collectCookedCultures`)의 표를 `<cookedDir>/<리소스 id>` 에 굽습니다.
         * @details 출력 하나가 쿠킹 작업 하나다 — 입력(프로젝트 · 원문 표 · 그 문화권 번역 표)은 작업 안의 읽기로 기록된다. 문화권 목록은 작업 밖에서 프로젝트 파일을 읽어 정한다.
         * @return 쓴(다시 쓴 것 포함) 표 수입니다. 실패는 @p outFailedCount 로 센다(프로젝트를 읽지 못함 · 화면에 나올 글이 없는 문화권 · 해시 충돌 · 쓰기 실패).
         */
        static uint32 cookAll( string_view sourceRoot, string_view cookedDir, const CookStore& store, uint32& outFailedCount );
        /**
         * @brief 프로젝트 @p projectID 를 새 관리자에 올려 @p culture 의 표 바이트를 만듭니다(쿠킹 작업 안에서 부른다 — 테스트도 이 길로 굽는다).
         * @return 프로젝트를 다 읽지 못했거나 그 문화권에 화면에 나올 글이 없거나 키 해시가 겹치면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool cookCulture( const AssetID& projectID, string_view culture, vector<uint8>& outBytes );
        /** @brief 작업 키의 쿠커 쪽 값입니다(쿠커 코드 지문 `Localization` — `Config/Dev/CookCodeInputs.json`). */
        static CookJobDesc makeCookJobDesc();
    };
} // namespace sw
