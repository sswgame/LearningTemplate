/**
 * @file App/EditorModuleHeadlessTasks.h
 * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 부르는 헤드리스 작업(임포트 · 로컬라이제이션 · 에셋 참조 검사)입니다. 부르는 곳은 `AppHeadlessTasks.cpp` 하나다.
 * @details 예전에는 `ClientModuleManager` 의 static 함수였다 — 인스턴스 상태를 쓰지 않고 모듈 라이브러리를 직접 올리고 내려 모듈 관리자의 일이 아니다.
 *          정의는 동적 모듈 구성에만 있다(`EditorModuleHeadlessTasks.cpp`). 부르는 자리가 `build::kStaticModules` 로 갈라 정적 구성에서는 버려지는 갈래다.
 *          헤더로 선언해 두는 까닭은 그것이다 — 한 파일 안 익명 함수면 정적 구성에서 정의 없는 내부 함수가 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "ModuleAPI/ABI/EditorAPI.h"

namespace sw
{
    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 @p kind 원본을 임포트하거나 대조합니다(`App --import-textures`, `--import-models` 와 `--check-*`).
     * @details 헤드리스 부팅(창과 RHI 없음)에서 부릅니다. 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 해제한 뒤 모듈을 언로드합니다.
     *          Shipping 에는 에디터 모듈이 없어 실패합니다. 인스턴스 상태를 쓰지 않아 모듈 관리자 밖에 둔다
     *          (모듈 라이브러리를 직접 올리고 내리는 의존이 인스턴스 경로에 섞이지 않게).
     * @return 원본과 임포트 결과가 맞으면(임포트는 모두 성공하면) true 를 반환합니다.
     */
    [[nodiscard]] bool importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly );
    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 로컬라이제이션 작업 @p task 를 실행합니다(`App --gather-text`, `--check-text`, `--import-po`, `--export-po`).
     * @details 임포트와 같은 경로입니다(`importAssetsWithEditorModule`). 타입 공급자 모듈이 모두 로드된 헤드리스 부팅에서 부릅니다. Shipping 에서는 실패합니다.
     * @param poPath `ImportPo` 가 읽을 PO 파일
     * @param projectArgument `-loc-project` 값(비면 기본 대상 전부)
     * @return 작업이 성공하면(확인 모드는 테이블이 최신이면) true 를 반환합니다.
     */
    [[nodiscard]] bool runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument );
    /**
     * @brief 에디터 인스턴스 없이 에디터 모듈만 로드해 에셋 참조를 검사합니다(`App --check-asset-refs`). 임포트와 같은 경로이고 Shipping 에서는 실패합니다.
     * @return 끊어진 참조 · 목록에 없는 `AssetPath` 프로퍼티가 하나도 없으면 true 를 반환합니다.
     */
    [[nodiscard]] bool validateAssetReferencesWithEditorModule();
} // namespace sw
