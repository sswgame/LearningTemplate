// motion_blur.hlsl — 모션 블러(언리얼 모션 블러 · 유니티 URP Motion Blur 의 오브젝트 모드와 같은 갈래). 이 픽셀의 화면 속도(SceneVelocity — 불투명 지오메트리 패스가 물체마다
// 지난 월드 행렬 · 스킨 위치로 쓴 이번 UV - 지난 UV, binding_output.hlsli)를 따라 원본을 고르게 섞는다. 카메라에 붙어 함께 움직이는 물체(1인칭 총)는 속도가 0 에 가까워 흐려지지 않는다.
// 그리지 않은 픽셀(하늘, 깊이 1)은 속도 출력이 없어 깊이로 복원한 월드 위치를 지난 뷰-투영(g_PrevViewProj)으로 다시 투영한다(카메라 움직임 — 돌면 하늘이 흐려진다).
// 끈 경우(사용자 설정 · 후처리를 끈 뷰 · 품질 미달 · 컷 · 카메라와 물체가 모두 서 있음)는 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
// shortcut: 속도를 타일 최댓값으로 늘이지 않는다(가장자리 밖으로 번지는 흐림이 없다) — 빠른 물체의 윤곽이 딱딱해 보이면 언리얼처럼 타일 최대 속도 패스를 더한다.
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const uint  kMotionBlurSampleCount = 8u;
static const float kMaxBlurUV             = 0.05f;   // 한 프레임 흐림 길이의 상한(화면 폭 비율) — 순간 이동 · 큰 회전이 화면을 통째로 문대지 않게
static const float kMinBlurUV             = 0.0005f; // 이보다 짧으면 섞지 않는다(정지 화면의 미세 떨림)
static const float kSkyDepth              = 1.0f;    // 깊이 클리어 값 — 아무것도 그리지 않은 픽셀(속도 출력이 없다)

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source = PassTexture::sampleSource( input._uv ).rgb;
    const float  depth  = PassTexture::sampleDepth( input._uv ).r;

    // 하늘의 카메라 재투영(TAA 와 같은 함수).
    float        previousClipW;
    const float2 prevUv         = Transform::computeCameraPreviousUv( input._uv, depth, previousClipW );
    const float2 cameraVelocity = input._uv - prevUv;
    const bool   bSky           = depth >= kSkyDepth;
    float2       velocity       = bSky ? cameraVelocity : PassTexture::sampleVelocityPoint( input._uv );
    const float  speed          = length( velocity );
    velocity *= ( speed > kMaxBlurUV ) ? ( kMaxBlurUV / speed ) : 1.0f;

    // 선분은 이 픽셀을 가운데에 두고 앞뒤로 반씩 편다(지난 위치 쪽 · 앞으로 갈 쪽).
    float3 accumulated = 0.0f;
    SW_UNROLL for ( uint sampleIndex = 0u; sampleIndex < kMotionBlurSampleCount; ++sampleIndex )
    {
        const float ratio = ( float( sampleIndex ) + 0.5f ) / float( kMotionBlurSampleCount ) - 0.5f;
        accumulated += PassTexture::sampleSource( input._uv + velocity * ratio ).rgb;
    }
    const float3 blurred = accumulated / float( kMotionBlurSampleCount );

    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — binding.hlsli MorphPool::computeElement).
    const bool bStill = speed < kMinBlurUV;
    return float4( bStill ? source : blurred, 1.0f );
}
