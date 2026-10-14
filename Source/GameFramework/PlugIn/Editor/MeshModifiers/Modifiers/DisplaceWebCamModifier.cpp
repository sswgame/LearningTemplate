/**
 * @file DisplaceWebCamModifier.cpp
 * @brief 웹캠 프레임 대신 텍스처 한 장을 표본해 그 밝기만큼 법선 방향으로 옮기는 모디파이어입니다.
 * @details 웹캠은 실행 환경 전용 입력이라 에디터 플러그인에서는 열 수 없다 — 그래서 정적 프레임(텍스처 파일 `Frame`)으로 대체한다.
 *          모양(매개변수 · 동작)은 웹캠 변위와 같게 두어, 실행 환경에서 웹캠 프레임을 넣는 쪽이 같은 값을 쓸 수 있다.
 *          높이 = 고른 채널(R · G · B · A · 밝기) - `Mid Level`, 이동 = 법선 × `Amount` × 높이. 웹캠 영상은 보통 좌우가 뒤집혀 있어 `Mirror U` 를 둔다.
 *          경로가 비었거나 양이 0 이거나 입력이 없거나 표본이 실패하면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct DisplaceWebCamModifierInternal
        {
            /** @brief 채널 번호(0 R · 1 G · 2 B · 3 A · 4 밝기)의 값입니다. */
            static float32 pickChannel( const float4& color, int32 channel )
            {
                switch ( channel )
                {
                    case 0:
                        return color._x;
                    case 1:
                        return color._y;
                    case 2:
                        return color._z;
                    case 3:
                        return color._w;
                    default:
                        return color._x * 0.2126f + color._y * 0.7152f + color._z * 0.0722f;
                }
            }
        };
    } // namespace

    /** @brief 웹캠 프레임을 정적 텍스처로 대체한 변위입니다. */
    class DisplaceWebCamModifier final : public MeshModifier
    {
    public:
        DisplaceWebCamModifier()
            : MeshModifier()
            , _frameParam{ addParam( MeshModifierParamDef{ "Frame", MeshModifierParamKind::AssetPath, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f }, 1,
                                                           "Static frame texture - webcam is runtime only, so a still frame stands in" }
                                                           .withLayout( "Frame", "Webcam input is runtime only, so a static frame texture stands in" ) ) }
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Displace", "Distance moved along the normal per unit height", "m" ) ) }
            , _midLevelParam{ addParam( MeshModifierParamDef{ "Mid Level", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Displace", "Frame value that causes no displacement" ) ) }
            , _channelParam{ addParam( MeshModifierParamDef{ "Channel", MeshModifierParamKind::Int, 0.0f, 4.0f, float3{ 4.0f, 0.0f, 0.0f }, 1,
                                                             "0 R 1 G 2 B 3 A 4 luminance" }
                                                             .withLayout( "Displace", "Frame channel used as height (0 R, 1 G, 2 B, 3 A, 4 luminance)" ) ) }
            , _mirrorParam{ addParam( MeshModifierParamDef{ "Mirror U", MeshModifierParamKind::Bool, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Mapping", "Flip the frame horizontally, as webcam images usually are" ) ) }
        {
        }

        const utf8* getDescription() const override
        {
            return "Displaces vertices along the normal by a frame channel. Webcam input is runtime only, so a static frame texture stands in.";
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32              amount  = getFloat( _amountParam );
            const string&              path    = getText( _frameParam );
            const IMeshModifierInputs* pInputs = context.getInputs();
            if ( MathUtil::abs( amount ) < MathUtil::kEpsilon || path.empty() || pInputs == nullptr )
                return point._position;
            const float2 uv{ getBool( _mirrorParam ) ? 1.0f - point._uv._x : point._uv._x, point._uv._y };
            float4       color{ 0.0f, 0.0f, 0.0f, 0.0f };
            if ( pInputs->trySampleTexture( path, uv, color ) == false )
                return point._position;
            const float32 height = DisplaceWebCamModifierInternal::pickChannel( color, getInt( _channelParam ) ) - getFloat( _midLevelParam );
            return point._position + point._normal * ( amount * height );
        }

    private:
        uint32 _frameParam;
        uint32 _amountParam;
        uint32 _midLevelParam;
        uint32 _channelParam;
        uint32 _mirrorParam;
    };

    SW_MESH_MODIFIER( DisplaceWebCamModifier, "displacewebcam", "Displace Web Cam (static frame - webcam is runtime only)", "Data" );
} // namespace sw::editor
