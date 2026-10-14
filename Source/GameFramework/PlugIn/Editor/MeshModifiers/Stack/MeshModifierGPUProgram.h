/**
 * @file MeshModifierGPUProgram.h
 * @brief 모디파이어 스택을 GPU 모프 컴퓨트가 실행할 프로그램(float4 목록)으로 바꿉니다. ImGui · 씬 · RHI 를 모릅니다.
 * @details 실행기는 엔진 셰이더 `Resource/engine/shaders/meshmodifier.hlsli` 이고 프로그램 모양은 그 머리 주석이 정본이다.
 *          프로그램은 `Mesh::setGPUDeformProgram` 으로 미리보기 메시에 걸리고 모프 풀이 프레임마다 정점에 건다(`meshmorph.hlsl`).
 *          CPU 스택이 정본이다 — GPU 가 다시 계산하지 못하는 값(모디파이어마다의 입력 상자 · 공간 행렬 · 기준 오브젝트)은 CPU 가 구해 싣는다.
 *          입력 상자는 같은 평가로 CPU 스택을 한 번 건 결과(`MeshModifierEvaluation::_pListInputBounds`)를 받는다.
 *
 *          GPU 로 옮기는 조건: 켜진 모디파이어가 모두 `supportsGPU()` 이고 종류 표(`findKind`)에 있으며, 스택이 법선 재계산을 고르지 않았고,
 *          메시에 선택 가중치 채널이 없다. 하나라도 어기면 `make` 가 false 이고 미리보기는 CPU 경로만 쓴다.
 *          새 점 단위 모디파이어를 GPU 로 옮기려면 셰이더의 `SW_MODIFIER_*` 번호와 함수, 이 파일의 표 한 줄을 함께 더한다(`MeshModifierGPUProgramTest` 가 둘이 맞는지 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    class MeshModifierStack;

    /** @brief 모디파이어 스택 → GPU 변형 프로그램 변환입니다. */
    struct MeshModifierGPUProgram
    {
        /** @brief 머리 float4 수입니다(x = 모디파이어 수, y = 시간). */
        static constexpr uint32 kHeaderFloat4Count = 1;
        /** @brief 모디파이어 기록 하나의 고정 float4 수입니다(`SW_MODIFIER_RECORD_FLOAT4`). 그 뒤에 매개변수가 붙습니다. */
        static constexpr uint32 kRecordFloat4Count = 17;

        /** @brief 종류 id 의 셰이더 종류 번호입니다. GPU 표에 없으면 -1 입니다. */
        static int32 findKind( string_view typeID );
        /** @brief GPU 표의 종류 id 수입니다. */
        static uint32 getKindCount();
        /** @brief 종류 번호의 id 입니다. 범위 밖이면 빈 글입니다. */
        static string_view getKindTypeID( uint32 kind );

        /**
         * @brief 스택이 GPU 로 옮길 수 있으면 true 입니다(`make` 의 조건 중 상자와 무관한 부분).
         * @param pOutBlockingIndex 막는 첫 모디파이어 번호를 받습니다(선택, 막는 것이 스택 설정이면 -1).
         */
        [[nodiscard]] static bool canMake( const MeshModifierStack& stack, int32* pOutBlockingIndex = nullptr );
        /**
         * @brief 프로그램을 만듭니다. 옮길 수 없거나 상자 수가 켜진 모디파이어 수와 다르면 false 이고 @p outProgram 은 비웁니다.
         * @param evaluation      CPU 적용과 같은 평가(메시 → 월드 · 외부 입력 · 시간)입니다.
         * @param listInputBounds 같은 평가로 CPU 스택을 건 동안 모은 입력 상자(`_pListInputBounds`)입니다.
         * @param bHasWeight      메시에 선택 가중치 채널이 있으면 true 입니다(GPU 는 가중치를 모른다).
         */
        [[nodiscard]] static bool make( const MeshModifierStack& stack, const MeshModifierEvaluation& evaluation, const vector<MeshModifierBounds>& listInputBounds,
                                        bool bHasWeight, vector<float4>& outProgram );
    };
} // namespace sw::editor
