/**
 * @file ProcessRoots.h
 * @brief 프로세스가 기동 때 한 번 정하는 루트 폴더 모음입니다(R5). 상대 경로는 실행 중 작업 폴더가 아니라 이 루트 기준으로 입력 경계에서 한 번 절대화한다.
 * @details 0 단계에서는 형식만 있고 아직 아무도 설치하지 않는다. 2 단계가 App · Server · 테스트 하네스 기동에서 `install` 하고, 지금 Saved 를 따로 구하는 일곱 곳을 `get()` 으로 바꾼다.
 *          못 정했으면 `get()` 이 멈춘다(폴백 없음 — 언리얼 `FPaths::ProjectSavedDir` 도 기동 때 정해진다).
 */
#pragma once
#include "Core/File/AbsolutePath.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /** @brief 루트 폴더들입니다. 모두 채워서 `ProcessRoots::install` 에 넘긴다. */
    struct ProcessRootSet
    {
        AbsolutePath binary;   ///< 실행 파일 폴더
        AbsolutePath project;  ///< 프로젝트(저장소) 루트 — `-project=`
        AbsolutePath content;  ///< 리소스 도메인 루트(`Content`)
        AbsolutePath saved;    ///< 개발 파생물(셰이더 캐시 · 임시)
        AbsolutePath logs;     ///< 로그
        AbsolutePath crash;    ///< 크래시 덤프
        AbsolutePath userData; ///< 사용자 설정 · 세이브
        AbsolutePath packs;    ///< 팩(`.pack`)
        AbsolutePath ddc;      ///< 파생 데이터 캐시
    };

    /**
     * @struct ProcessRoots
     * @brief 프로세스 하나의 루트 모음입니다. 기동 때 한 번 `install` 하고 이후 읽기만 한다.
     */
    struct SW_API ProcessRoots
    {
        /** @brief 루트를 설치합니다. 두 번 부르면 멈춥니다(기동 한 번). */
        static void install( ProcessRootSet roots );
        /** @brief 설치됐는지 확인합니다(테스트 · 기동 순서 점검용). */
        [[nodiscard]] static bool isInstalled();
        /** @brief 설치된 루트입니다. 설치 전이면 멈춥니다. */
        [[nodiscard]] static const ProcessRootSet& get();
    };
} // namespace sw
