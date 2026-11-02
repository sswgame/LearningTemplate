/**
 * @file SPIRVOpenGLPatch.h
 * @brief DXC 가 낸 SPIR-V 를 OpenGL(ARB_gl_spirv)이 받는 모양으로 고칩니다. `ShaderCompiler` 가 GL 타깃에만 부릅니다.
 * @details DXC 는 Vulkan 용 SPIR-V 를 낸다 — GL 드라이버와 다른 둘을 여기서 맞춘다. 패치를 바꾸면 `ShaderCookStamp::kCompileOptionsRevision` 을 올린다(쿠킹한 GL 바이너리가 낡는다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief GL 용 SPIR-V 패치입니다(전부 static, 바이트 배열을 제자리에서 고친다). 매직이 아니거나 깨진 모듈은 건드리지 않는다. */
    struct SW_API SPIRVOpenGLPatch
    {
        /** @brief 아래 셋을 차례로 겁니다. */
        static void apply( vector<uint8>& ioBytecode );

        /**
         * @brief Vulkan 전용 내장 변수를 GL 것으로 바꿉니다(InstanceIndex→InstanceId, VertexIndex→VertexId).
         * @details DXC 는 SV_InstanceID/SV_VertexID 를 Vulkan 의 InstanceIndex/VertexIndex 로 냅니다. ARB_gl_spirv 는 그 둘을
         *          지원하지 않아 바꾸지 않으면 드라이버가 조용히 0 을 반환합니다(인스턴스드 드로우가 모두 원소 0 을 읽어 한
         *          자리에 겹칩니다). 지금 셰이더는 인스턴스 자리를 정점 스트림(`SW_INSTANCESLOT`)으로 받아 SV_InstanceID 를 쓰지
         *          않습니다 — 다시 쓰게 되면 gl_InstanceID 는 startInstance 를 빼고 센다는 점(Vulkan InstanceIndex 와 다르다)에
         *          주의하십시오. OpDecorate(71) / OpMemberDecorate(72) 의 BuiltIn(11) 만 손댑니다.
         */
        static void patchBuiltins( vector<uint8>& ioBytecode );

        /**
         * @brief 이름(OpMemberName)이 없는 구조체 멤버에 `_m<번호>` 를 붙입니다.
         * @details DXC 는 StructuredBuffer 를 감싼 블록 구조체(`type.StructuredBuffer.T`)의 멤버 0(런타임 배열)에 이름을 달지 않는다. GL 은 단계 사이 같은 블록을
         *          이름으로 맞추는데, NVIDIA 드라이버는 없는 이름을 SPIR-V 번호로 지어(`_struct11_member0`) 정점 · 픽셀 단계의 같은 블록을 다른 것으로 보고 링크를 거절한다
         *          (AMD 는 넘겨 숨어 있었다). 이름은 타입 안의 번호라 단계와 상관없이 같다. 새 명령은 디버그 이름 절 끝(첫 장식 · 타입 명령 앞)에 넣는다 — SPIR-V 의 절 순서다.
         */
        static void nameStructMembers( vector<uint8>& ioBytecode );

        /**
         * @brief 조각 셰이더의 좌표 원점을 `OriginLowerLeft` 로 바꿉니다(DXC 는 Vulkan 규칙대로 `OriginUpperLeft` 를 낸다).
         * @details GL 의 오프스크린 대상은 클립 원점을 GL_UPPER_LEFT 로 걸어 메모리 행 순서를 DX 와 맞춘다(OpenGLRHICommandContext). ARB_clip_control 은 `gl_FragCoord` 를 바꾸지 않으므로
         *          그 대상에서 DX 의 `SV_Position.y`(위에서 센 행 + 0.5)와 같은 값은 왼쪽 아래 원점이다. `OriginUpperLeft` 를 남기면 규격대로인 드라이버(AMD)는 y 를 H - y 로 뒤집어
         *          픽셀 좌표 무늬(디더)가 어긋났고, 그렇지 않은 드라이버(NVIDIA)만 우연히 맞았다. 기본 프레임버퍼(클립 원점 LOWER_LEFT)에 그리는 셰이더는 `SV_Position` 을 쓰지 않는다(fsr1 은 UV).
         */
        static void useLowerLeftFragmentOrigin( vector<uint8>& ioBytecode );
    };
} // namespace sw
