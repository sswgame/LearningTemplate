/**
 * @file RHIEnums.h
 * @brief RHI 열거형입니다: 픽셀 포맷, 버퍼 용도와 상태, 파이프라인 상태, 텍스처 형태, 전송 결과.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RHIFormat
     * @brief 텍스처, 렌더 타깃, 픽셀 데이터의 포맷입니다.
     */
    ENUM()
    enum class RHIFormat : uint32
    {
        R8G8B8A8_UNORM     = 0, ///< 8비트 RGBA 정규화
        B8G8R8A8_UNORM     = 1, ///< 8비트 BGRA 정규화 (`-gv_rhiBackBufferFormat=BGRA8` 로 고르는 백버퍼 포맷)
        R16G16B16A16_FLOAT = 2, ///< 16비트 부동소수점 RGBA (HDR)
        D24_UNORM_S8_UINT  = 3, ///< 24비트 깊이 + 8비트 스텐실
        R32G32B32_FLOAT    = 4, ///< 32비트 부동소수점 RGB (위치/노멀)
        R32G32_FLOAT       = 5, ///< 32비트 부동소수점 RG (UV)
        R32_FLOAT          = 6, ///< 32비트 단일 부동소수점
        /// @brief 첨부 없음입니다. 뎁스를 쓰지 않는 패스의 PSO 가 "DSV 없음" 을 선언할 때 씁니다.
        ///        DX12 는 PSO 뎁스 포맷이 UNKNOWN 일 때만 null DSV 를 허용합니다.
        ///        기존 값의 번호는 바꾸지 않습니다. 새 포맷은 아래처럼 뒤에 이어서 추가합니다.
        Unknown = 7,
        // 블록 압축(4x4 블록). 렌더 타깃으로 쓸 수 없고, 샘플링, 업로드, 읽기만 가능하다. DDS 의 DXT1/3/5, BC4/5/6H/7 과 1:1 로 대응한다.
        BC1_UNORM = 8,  ///< 8 B/블록, RGB + 1비트 알파
        BC2_UNORM = 9,  ///< 16 B/블록, 명시 알파
        BC3_UNORM = 10, ///< 16 B/블록, 보간 알파
        BC4_UNORM = 11, ///< 8 B/블록, 단일 채널
        BC5_UNORM = 12, ///< 16 B/블록, 2채널(노멀맵)
        BC7_UNORM = 13, ///< 16 B/블록, 고품질 RGBA
        BC6H_UF16 = 14, ///< 16 B/블록, 부호 없는 HDR RGB(반정밀도) — `.hdr` 원본 임포트(하늘 · 환경광)
        R8_UNORM  = 15, ///< 8비트 단일 채널 정규화(글리프 SDF 아틀라스 · 마스크). 셰이더는 `.r` 로 읽는다
        /// @brief 16비트 부호 있는 정규화 RG(4 B) — G버퍼 법선의 옥타헤드럴 좌표. 셰이더는 `.rg` 가 [-1, 1] 실수다.
        ///        컬러 첨부 지원은 Vulkan 필수 포맷이 아니라 장치 생성 때 조회한다(`PackedTypes.md` P4).
        R16G16_SNORM = 16,
    };

    /**
     * @enum RHIBackBufferFormat
     * @brief 요청할 수 있는 백버퍼 포맷입니다(`-gv_rhiBackBufferFormat=BGRA8`). 언리얼 `r.DefaultBackBufferPixelFormat` 과 같은 자리입니다.
     * @details `RHIFormat` 전체가 아니라 백버퍼로 쓸 수 있는 둘만 담는다 — 깊이 · 압축 포맷을 요청하는 모양을 만들 수 없게 한다.
     *          리플렉션 enum 이라 명령줄이 이름으로 고른다. 숫자는 받지 않는다(`GlobalVariableInfo::setValueFromCommandLine`).
     *          백엔드가 실제로 채택한 값은 `IRHIDevice::getBackBufferFormat()` 이 답한다.
     */
    ENUM()
    enum class RHIBackBufferFormat : uint8
    {
        RGBA8, ///< R8G8B8A8_UNORM — 계약 기본값(`constant::kBackBufferFormat`)
        BGRA8, ///< B8G8R8A8_UNORM — 백버퍼를 타깃으로 하는 PSO 가 이 포맷으로도 만들어지는지 돌려 보는 스위치
    };

    /**
     * @enum RHIBufferUsage
     * @brief 범용 GPU 버퍼의 용도 플래그입니다(createBuffer).
     */
    ENUM( Flags )
    enum class RHIBufferUsage : uint8
    {
        None            = 0,
        Vertex          = SW_BIT( 0 ),
        Index           = SW_BIT( 1 ),
        Constant        = SW_BIT( 2 ),
        Structured      = SW_BIT( 3 ),
        UnorderedAccess = SW_BIT( 4 ),
        ShaderResource  = SW_BIT( 5 ),
        IndirectArgs    = SW_BIT( 6 ),
        Raw             = SW_BIT( 7 ), ///< 바이트 주소 / RAW UAV (DX11 DRAWINDIRECT_ARGS 호환)
    };

    /**
     * @enum RHIBufferState
     * @brief transitionBuffer 배리어에 쓰는 논리 버퍼 상태입니다.
     */
    enum class RHIBufferState : uint32
    {
        Common           = 0,
        ShaderResource   = 1,
        UnorderedAccess  = 2,
        IndirectArgument = 3,
        CopyDest         = 4,
        VertexOrConstant = 5,
        Index            = 6,
        CopySource       = 7, ///< `IRHICommandList::copyBufferToReadback` 의 원본. 복사가 끝나면 쓰던 상태로 되돌린다
    };

    /**
     * @enum RHIBlendMode
     * @brief 머티리얼과 패스의 블렌드 분류입니다.
     * @details 리플렉션 enum 이다. 메시 컴포넌트의 `_blendMode` PROPERTY 가 이 이름으로 저장된다. `ENUM()` 을 빼면 직렬화기가 이름을
     *          몰라 `"null"` 로 적고, 읽을 때 불투명으로 돌아간다(언리얼 `EBlendMode` 도 UENUM 이다).
     */
    ENUM()
    enum class RHIBlendMode : uint8
    {
        Opaque      = 0,
        Transparent = 1, ///< SrcAlpha / InvSrcAlpha
    };

    /**
     * @enum RHIPrimitiveTopology
     * @brief 프리미티브 토폴로지입니다.
     */
    enum class RHIPrimitiveTopology
    {
        TriangleList, ///< 삼각형 리스트
        LineList,     ///< 선 리스트
        PointList     ///< 점 리스트
    };

    /**
     * @enum RHIFillMode
     * @brief 래스터라이저의 채우기 방식(솔리드, 와이어프레임)입니다.
     */
    enum class RHIFillMode
    {
        Solid,    ///< 일반 채우기
        Wireframe ///< 와이어프레임
    };

    /**
     * @enum RHICullMode
     * @brief 면 컬링 방식입니다.
     */
    enum class RHICullMode
    {
        None,  ///< 컬링 없음
        Front, ///< 전면 컬링
        Back   ///< 후면 컬링
    };

    /**
     * @enum RHIRenderPassLoadOp
     * @brief 렌더 패스를 시작할 때 첨부를 어떻게 읽을지 정합니다.
     */
    enum class RHIRenderPassLoadOp
    {
        Clear,   ///< 기존 데이터를 지운다.
        Load,    ///< 기존 데이터를 유지한다.
        DontCare ///< 이전 내용을 무시한다(최적화).
    };

    /**
     * @enum RHIRenderPassStoreOp
     * @brief 렌더 패스를 마칠 때 첨부를 어떻게 저장할지 정합니다.
     */
    enum class RHIRenderPassStoreOp
    {
        Store,   ///< 최종 결과를 메모리에 저장한다.
        DontCare ///< 결과를 보존하지 않는다.
    };

    /**
     * @enum RHITextureDimension
     * @brief 텍스처의 형태입니다. 세 가지 모두 2D 면(slice)을 겹친 것이고, 면 수와 셰이더가 보는 뷰만 다릅니다.
     *        D3D 의 Texture2D + ArraySize, TEXTURECUBE 와 같으며, 그래서 텍스처를 만드는 함수는 `createTexture2D` 하나입니다.
     * @details 면 하나는 렌더 패스의 타깃(`RHIRenderPassBeginInfo::_arrColorTargetSlice`, `_depthTargetSlice`)이 될 수 있고, 업로드와
     *          읽기도 면 단위로 한다(`RHITextureUploadDesc::_arraySlice`, `submitTextureReadback` 의 slice). 셰이더에 바인딩하는
     *          bindless 등록은 아직 Texture2D 만 받는다. 배열이나 큐브 뷰를 읽는 셰이더 테이블이 생기면 넓힌다(점광 그림자).
     */
    enum class RHITextureDimension : uint8
    {
        Texture2D = 0,  ///< 면 하나
        Texture2DArray, ///< 면 `_arraySize` 개
        TextureCube,    ///< 면 6개(+X, −X, +Y, −Y, +Z, −Z 순서이며 D3D, Vulkan, GL 과 같다)
    };

    /**
     * @enum RHISubmitResult
     * @brief 전송 제출 함수의 결과입니다.
     */
    enum class RHISubmitResult : uint8
    {
        /// 검사에 걸렸거나 이 요청은 어떤 상황에서도 받을 수 없다(포맷, 범위, 데이터 크기, 잘못된 핸들, 스테이징 링과 단독 버퍼 총량 상한보다 큰 요청, 장치 손실). 이유는 로그에 있다. 다시 시도하지 않는다.
        Rejected = 0,
        Accepted = 1, ///< 받았다. 소스 메모리는 이미 복사됐다. 티켓이 유효하다.
                      /// 지금은 스테이징이 모자라지만 GPU 가 진행하면 받을 수 있다. 기다리지 않고 돌아왔으니 호출자가 다음 프레임에 다시 제출한다. 티켓은 무효다. 다시 시도해도 영원히 안 되는 요청은 `Rejected` 다.
        Busy = 2,
    };

    /**
     * @enum RHITransferStatus
     * @brief 제출한 전송의 진행 상태입니다(`IRHIResourceFactory::pollTransfer`).
     */
    enum class RHITransferStatus : uint8
    {
        Pending  = 0, ///< GPU 가 아직 끝내지 않았다. 프레임을 돌리지 않아도 `pollTransfer` 를 되풀이하면 결국 `Complete` 나 `Failed` 가 된다.
        Complete = 1, ///< 끝났다. 같은 티켓을 다시 물어도 Complete 다(완료 워터마크 이상으로 판정한다).
        Failed   = 2, ///< 장치 손실처럼 끝나지 않을 일이다. 오류는 이미 로그에 남았고 대기하지 않는다. 무효 티켓, 다른 장치 세대의 티켓, 발급한 적 없는 시리얼도 Failed 다.
    };
} // namespace sw
