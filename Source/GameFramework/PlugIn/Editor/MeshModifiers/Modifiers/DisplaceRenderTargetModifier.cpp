/**
 * @file DisplaceRenderTargetModifier.cpp
 * @brief 텍스처를 정점 UV 에서 표본해 그 높이만큼 법선 방향으로 옮기는 모디파이어입니다(렌더 타깃 변위의 베이크판).
 * @details 이름은 실시간 렌더 타깃 변위에서 왔지만, 에디터 베이크에는 매 프레임 그려지는 렌더 타깃이 없다 — 그래서 정적 텍스처 파일(`Texture`)로 대체한다.
 *          텍스처는 외부 입력 창구 `IMeshModifierInputs::trySampleTexture` 로 읽는다. 높이 = 고른 채널(R · G · B · A · 밝기) - `Mid Level`,
 *          이동 = 법선 × `Amount` × 높이. UV 는 `Tiling` · `Offset` 으로 옮긴 뒤 표본한다.
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
        /** @brief 채널 번호(0 R · 1 G · 2 B · 3 A · 4 밝기)의 값입니다. */
        float32 pickRenderTargetChannel( const float4& color, int32 channel )
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
    } // namespace

    /** @brief 정적 텍스처로 대체한 렌더 타깃 변위입니다. */
    class DisplaceRenderTargetModifier final : public MeshModifier
    {
    public:
        DisplaceRenderTargetModifier()
            : MeshModifier()
            , _textureParam{
                  addParam( MeshModifierParamDef{ "Texture", MeshModifierParamKind::AssetPath, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f }, 1,
                                                 "Static texture used in place of the runtime render target" }
                                .withLayout( "Texture", "Static texture that replaces the realtime render target for editor baking" )
                                // 확장자 목록은 Editor/Common/Workspace/EditorAssetType.cpp 의 kArrTextureExt 와 같다.
                                .withAssetFilter( ".png,.jpg,.jpeg,.tga,.dds,.hdr,.bmp" ) )
        },
            _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Displace", "Distance moved along the normal per unit height", "m" ) ) },
            _midLevelParam{ addParam( MeshModifierParamDef{ "Mid Level", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Displace", "Texture value that causes no displacement" ) ) },
            _channelParam{ addParam( MeshModifierParamDef{ "Channel", MeshModifierParamKind::Int, 0.0f, 4.0f, float3{ 4.0f, 0.0f, 0.0f }, 1, "0 R 1 G 2 B 3 A 4 luminance" }.withLayout( "Displace", "Texture channel used as height (0 R, 1 G, 2 B, 3 A, 4 luminance)" ) ) },
            _tilingParam{ addParam( MeshModifierParamDef{ "Tiling", MeshModifierParamKind::Vector3, -100.0f, 100.0f, float3{ 1.0f, 1.0f, 0.0f }, 1, "u v (z unused)" }.withLayout( "Mapping", "UV repeat count (z unused)" ) ) },
            _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Vector3, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "u v (z unused)" }.withLayout( "Mapping", "UV offset (z unused)" ) ) }
        {
        }

        const utf8* getDescription() const override
        {
            return "Displaces vertices along the normal by a texture channel. The realtime render target is replaced by a static texture for editor baking.";
        }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32              amount          = getFloat( _amountParam );
            const string&              path            = getText( _textureParam );
            const IMeshModifierInputs* pInputs         = context.getInputs();
            const bool                 bNoDisplacement = MathUtil::abs( amount ) < MathUtil::kEpsilon || path.empty() || pInputs == nullptr;
            if ( bNoDisplacement )
                return point._position;
            const float3& tiling = getVector3( _tilingParam );
            const float3& offset = getVector3( _offsetParam );
            const float2  uv{ point._uv._x * tiling._x + offset._x, point._uv._y * tiling._y + offset._y };
            float4        color{ 0.0f, 0.0f, 0.0f, 0.0f };
            if ( pInputs->trySampleTexture( path, uv, color ) == false )
                return point._position;
            const float32 height = pickRenderTargetChannel( color, getInt( _channelParam ) ) - getFloat( _midLevelParam );
            return point._position + point._normal * ( amount * height );
        }

    private:
        uint32 _textureParam;
        uint32 _amountParam;
        uint32 _midLevelParam;
        uint32 _channelParam;
        uint32 _tilingParam;
        uint32 _offsetParam;
    };

    SW_MESH_MODIFIER( DisplaceRenderTargetModifier, "displacerendertarget", "Displace Render Target (static texture)", "Data" );
} // namespace sw::editor
