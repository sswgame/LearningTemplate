/**
 * @file CookCodeFingerprint.h
 * @brief 쿠커 코드의 내용 지문입니다. 빌드가 소스를 해시해 만듭니다(`Scripts/generate/cook_code_fingerprint.py`, 입력 목록 `Config/Dev/CookCodeInputs.json`).
 * @details 쿠킹 재사용 판단에 쿠커 번호를 사람이 올리지 않는다 — 언리얼 DDC 의 손 버전(`DDC_VER`)은 올리는 것을 잊으면 옛 결과를 쓴다. 코드가 바뀌면 지문이 바뀌어 저절로 다시 굽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 생성 표의 한 줄입니다. */
    struct CookCodeFingerprintRow
    {
        const utf8* _pName;
        uint64      _fingerprint;
    };

    /** @struct CookCodeFingerprint @brief 생성 표를 읽습니다. 지문이 바뀌면 이 파일의 .cpp 만 다시 컴파일됩니다. */
    struct SW_API CookCodeFingerprint
    {
        /** @brief 입력 목록의 이름 @p name 의 지문입니다. 목록에 없으면 0 입니다(`CookJob` 이 0 을 받지 않는다). */
        [[nodiscard]] static uint64 find( string_view name );
        /** @brief 설치된 셰이더 컴파일러의 버전 글입니다(`directx-dxc <판>`, vcpkg 설치 기록에서 읽는다). */
        [[nodiscard]] static const utf8* getShaderCompilerVersion();
    };
} // namespace sw
