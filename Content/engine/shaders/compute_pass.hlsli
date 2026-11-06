// compute_pass.hlsli — 파이프라인 XML 의 컴퓨트 패스(`_type="Compute"`)가 받는 값입니다. C++: FramePassExecutor::executeComputePass.
// 첨부는 RW 텍스처로 묶인다 — 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 읽지 못한다. 쓰는 쪽(슬롯 0)이 출력, 그 뒤가 선언한 입력 순서다.
// GL 은 묶은 포맷과 셰이더 선언이 같아야 하므로, 이 파일을 include 하기 전에 슬롯 포맷을 적는다:
//     #define SW_RW_SLOT0_GL_FORMAT "rgba16f"
// numthreads 는 XML 의 `_threadGroupSize` 와 같아야 한다.
#ifndef SW_COMPUTE_PASS_HLSLI
#define SW_COMPUTE_PASS_HLSLI

#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_ComputeOutputIndex; // 출력의 RW 번호(네이티브) · RW 슬롯 서수(에뮬)
    uint g_ComputeInput0Index; // 첫 입력(없으면 kInvalidIndex)
    uint g_ComputeInput1Index; // 둘째 입력(없으면 kInvalidIndex)
    uint g_ComputeWidth;       // 출력 크기(px)
    uint g_ComputeHeight;
};
SW_ROOT_CONSTANTS_END

#endif
