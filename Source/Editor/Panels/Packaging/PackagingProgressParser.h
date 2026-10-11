/**
 * @file PackagingProgressParser.h
 * @brief 패키징 진입점(`Scripts/dev/MakePackage.py`)의 출력 줄을 읽어 진행과 결과로 바꿉니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /** @brief 패키징 한 번의 상태입니다. 탐침 `Editor.PackagingState` 의 값이기도 하다. */
    enum class PackagingState : uint8
    {
        Idle = 0,
        Running,
        Succeeded,
        Failed
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 출력에서 읽은 진행입니다. */
    struct PackagingProgress
    {
        string         _stepName;     ///< 마지막으로 시작한 단계(`build` · `cook` · `stage` · `verify`)
        string         _outputFolder; ///< 끝 줄의 패키지 폴더
        string         _failure;      ///< 실패 줄의 이유
        uint64         _byteCount{ 0 };
        uint32         _stepIndex{ 0 }; ///< 시작한 단계(1 부터, 아직 없으면 0)
        uint32         _stepCount{ 0 };
        PackagingState _state{ PackagingState::Idle };

        /** @brief 진행 막대 값(0..1)입니다. 끝났으면 1 입니다. */
        float32 computeFraction() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct PackagingProgressParser
     * @brief `[package] step k/n <단계>` · `[package] done <폴더> <바이트>` · `[package] FAILED <단계> <이유>` 줄을 읽습니다. 다른 줄은 지나칩니다.
     */
    struct PackagingProgressParser
    {
        /** @brief 줄 하나를 @p inoutProgress 에 반영합니다. 진입점의 줄이면 true 입니다. */
        [[nodiscard]] static bool parseLine( string_view line, PackagingProgress& inoutProgress );
        /**
         * @brief 끝난 실행의 출력 전부를 읽습니다.
         * @details 끝 줄 없이 끝났으면(파이썬을 못 띄움 · 예외) 종료 코드로 실패를 정하고 이유에 마지막 줄을 둔다.
         */
        static PackagingProgress parseOutput( const vector<string>& listLine, int32 exitCode );
    };
} // namespace sw::editor
