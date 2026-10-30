/**
 * @file ProcessRoots.h
 * @brief 프로세스의 루트 폴더 모음입니다(R5). 실행 파일 위치로 **한 번** 정하고 실행 중 작업 폴더는 보지 않는다.
 * @details 예전에는 저장 폴더(`Saved`) 기준이 셋이었다 - 작업 폴더 아래(`getSavedFolderPath` · 상대 `"Saved/…"`), 실행 파일 폴더 아래(셰이더 캐시),
 *          프로젝트 아래(DDC · 쿠킹 저장소 · 트레이스) - 이제 실행 산출물(`_saved`)과 구성이 함께 쓰는 파생물(`_derivedData`) 둘을 이름으로 가른다. 작업 폴더를 바꾸고 띄우면 산출물이 다른 곳에 쌓였다. 여기 한 곳이 답한다.
 *          못 정하면(실행 파일 경로를 모름 · 위로 올라가도 `Content` 가 없음) 처음 묻는 자리에서 멈춘다(폴백 없음). 언리얼 `FPaths::ProjectSavedDir` 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /**
     * @struct ProcessRoots
     * @brief 프로세스의 루트 폴더들입니다. `get()` 이 처음 부를 때 실행 파일 위치로 정한다.
     */
    struct SW_API ProcessRoots
    {
        AbsolutePath _binary;      ///< 엔진 바이너리 폴더(`Bin` - 테스트 · 도구 실행 파일도 형제 `Bin`, `FileUtil::getBinaryDirectory`)
        AbsolutePath _project;     ///< 프로젝트 루트 - 바이너리 폴더에서 위로 올라가 `Content` 가 있는 첫 폴더
        AbsolutePath _content;     ///< 리소스 도메인 루트(`<프로젝트>/Content`)
        AbsolutePath _saved;       ///< 실행 산출물 · 개발 캐시(`<바이너리>/Saved` - 로그 · 스크린샷 · 셰이더 캐시 · 자동화)
        AbsolutePath _derivedData; ///< 빌드 구성(Debug · Release)이 함께 쓰는 파생물(`<프로젝트>/Saved` - DDC · 쿠킹 기록 · 개발 인증서)

        /** @brief 루트들입니다. 처음 부를 때 정하고 그 뒤로 바뀌지 않는다. 정하지 못하면 멈춥니다. */
        [[nodiscard]] static const ProcessRoots& get();
    };
} // namespace sw
