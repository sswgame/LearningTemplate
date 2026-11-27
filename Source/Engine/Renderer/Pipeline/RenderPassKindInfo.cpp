#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Debug/GPUMessage.h"
#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    static_assert( RenderPassKindTable::isTableConsistent( kRenderPassKindTable ), "RenderPassKindInfo 표의 줄이 규칙을 어긴다" );

    void reportRenderPassKindOutOfRange( uint32 value )
    {
        reportFatalError( "RenderPassKind %# is outside the pass kind table (%# kinds) - a bad cast or a stale enum value reached the renderer", value, kRenderPassKindCount );
    }

    const MaterialPassFilter& RenderPassKindInfo::getDefaultMaterialFilter() const
    {
        // 표 값은 글자 상수라 처음 한 번 글로 옮겨 둔다 — 드로우 루프가 프레임마다 묻는다.
        static const vector<MaterialPassFilter> s_listFilter = []
        {
            vector<MaterialPassFilter> listFilter( kRenderPassKindCount );
            for ( uint32 typeIndex = 0; typeIndex < kRenderPassKindCount; ++typeIndex )
            {
                const RenderPassKindInfo& info = kRenderPassKindTable._arrRow[typeIndex];
                if ( info._pRequiredMaterialDefine != nullptr )
                    listFilter[typeIndex]._requiredDefine = info._pRequiredMaterialDefine;
                if ( info._pExcludedMaterialDefine != nullptr )
                    listFilter[typeIndex]._excludedDefine = info._pExcludedMaterialDefine;
            }
            return listFilter;
        }();
        return s_listFilter[static_cast<uint32>( _type )]; // 줄은 표에서만 나오므로 범위 안이다
    }

    bool passWritesVelocity( RenderPassKind type, const RenderGraphPassDesc* pTargetDesc )
    {
        if ( pTargetDesc == nullptr || getRenderPassKindInfo( type ).hasFlag( RenderPassTraitFlag::kWritesVelocity ) == false )
            return false;
        return VectorUtil::containsIf( pTargetDesc->_listResolvedColorOutput, []( const RenderGraphPassDesc::ResolvedAttachment& output )
        { return static_cast<RenderPassInputRole>( output._role ) == RenderPassInputRole::Velocity; } );
    }

    RenderPassShaderSelection selectRenderPassShader( RenderPassKind type, const RenderGraphPassDesc* pPassDesc, const RenderGraphPassDesc* pTargetDesc,
                                                      const EngineDefaultAssets& engineDefaultAssets )
    {
        const RenderPassKindInfo& info = getRenderPassKindInfo( type );

        RenderPassShaderSelection selection;
        if ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false )
            selection._shaderPath = pPassDesc->_shaderPath.path();
        else if ( info._pDefaultShader != nullptr )
            selection._shaderPath = ( engineDefaultAssets.*info._pDefaultShader ).get();

        if ( pPassDesc != nullptr )
            selection._listDefine = pPassDesc->_listPermutation;
        if ( info._pPassDefine != nullptr )
        {
            const string passDefine{ info._pPassDefine };
            VectorUtil::addUnique( selection._listDefine, passDefine );
        }
        // 씬 메시를 그리는 패스 PSO 는 머티리얼 없는 배치를 그린다 — 머티리얼 칸 0 을 적는다(웹은 칸 수가 없으면 머티리얼 칸 읽기가 정의되지 않는다).
        // 머티리얼 변형은 같은 이름을 머티리얼 칸 수로 바꾼다(MaterialPermutation::mergeDefine).
        if ( info.hasFlag( RenderPassTraitFlag::kDrawsSceneMeshes ) )
            selection._listDefine.push_back( MaterialPermutation::makeTextureCountDefine( 0 ) );
        // 화면 속도 — 출력 선언(데이터)이 정한다. 내부 슬롯은 대신 그리는 패스의 선언을 본다(컬러 포맷과 같은 근거).
        if ( passWritesVelocity( type, pTargetDesc ) )
            selection._listDefine.push_back( kPassVelocityDefine );
        // 셰이더 메시지 변형 — 요구되지 않으면(Shipping 은 늘) define 도 PSO 변형도 없다.
        if ( info.hasFlag( RenderPassTraitFlag::kGPUMessageTarget ) && GPUMessageChannel::isRequested() )
            selection._listDefine.push_back( kGPUMessagesDefine );
        return selection;
    }

    RenderPassShaderSelection selectRenderPassComputeShader( const RenderGraphPassDesc& passDesc, const EngineDefaultAssets& engineDefaultAssets )
    {
        RenderPassShaderSelection selection = selectRenderPassShader( passDesc._type, &passDesc, &passDesc, engineDefaultAssets );
        const string              computeDefine{ kPassComputeImplementationDefine };
        VectorUtil::addUnique( selection._listDefine, computeDefine );
        return selection;
    }

    RenderPassShaderStages enumeratePassShaderStages( RenderPassKind type, const RenderGraphPassDesc* pPassDesc, const RenderGraphPassDesc* pTargetDesc,
                                                      const vector<RenderPassAttachment>& listAttachment, const EngineDefaultAssets& engineDefaultAssets )
    {
        const RenderPassKindInfo&       info   = getRenderPassKindInfo( type );
        const RenderPassShaderSelection shader = selectRenderPassShader( type, pPassDesc, pTargetDesc, engineDefaultAssets );
        RenderPassShaderStages          stages;
        if ( shader._shaderPath.empty() )
            return stages;
        const auto add = [&stages, &shader]( ShaderStage stage, string_view entryPoint )
        { stages._listStage.push_back( RenderPassShaderStage{ stage, shader._shaderPath, string( entryPoint ), shader._listDefine } ); };
        const auto resolvePassEntryPoint = [pPassDesc]( string RenderGraphPassDesc::* pEntryPoint, ShaderStage stage )
        { return resolveEntryPoint( pPassDesc != nullptr ? string_view( pPassDesc->*pEntryPoint ) : string_view{}, stage ); };

        if ( info.isComputePass() )
        {
            add( ShaderStage::Compute, resolvePassEntryPoint( &RenderGraphPassDesc::_computeEntryPoint, ShaderStage::Compute ) );
            return stages;
        }
        add( ShaderStage::Vertex, resolvePassEntryPoint( &RenderGraphPassDesc::_vertexEntryPoint, ShaderStage::Vertex ) );
        // 컬러 출력이 없는 패스(그림자 · 깊이 프리패스)는 픽셀 스테이지가 없다 — 그 위에 머티리얼 define 을 얹은 변형도 같은 판정을 물려받는다. 그림자 캐시 복원(kWritesPixelDepth)만 컬러 없이 픽셀 깊이를 쓴다.
        const bool   bDeclaredColor = info._pColorFormat == nullptr && pTargetDesc != nullptr;
        const uint32 colorCount     = bDeclaredColor ? pTargetDesc->collectColorOutputFormats( listAttachment, nullptr, kMaxColorAttachments, info._colorTargetCount ) : info._colorTargetCount;
        if ( colorCount > 0 || info.hasFlag( RenderPassTraitFlag::kWritesPixelDepth ) )
            add( ShaderStage::Pixel, resolvePassEntryPoint( &RenderGraphPassDesc::_pixelEntryPoint, ShaderStage::Pixel ) );
        if ( pPassDesc == nullptr )
            return stages;
        // 선택 스테이지는 진입점을 적은 패스만 쓴다.
        if ( pPassDesc->_geometryEntryPoint.empty() == false )
            add( ShaderStage::Geometry, pPassDesc->_geometryEntryPoint );
        if ( pPassDesc->_hullEntryPoint.empty() == false )
            add( ShaderStage::Hull, pPassDesc->_hullEntryPoint );
        if ( pPassDesc->_domainEntryPoint.empty() == false )
            add( ShaderStage::Domain, pPassDesc->_domainEntryPoint );
        if ( pPassDesc->_implementation == RenderPassExecutionMode::Compute && info.hasComputeImplementation() )
        {
            RenderPassShaderSelection computeShader = selectRenderPassComputeShader( *pPassDesc, engineDefaultAssets );
            stages._computeImplementation           = RenderPassShaderStage{ ShaderStage::Compute, std::move( computeShader._shaderPath ),
                                                                   string( getShaderStageInfo( ShaderStage::Compute )._pEntryPoint ), std::move( computeShader._listDefine ) };
        }
        return stages;
    }

    RenderPassShaderSelection selectDeferredTileLightingShader( const RenderGraphPassDesc& lightingDesc, uint32 tileClass, const EngineDefaultAssets& engineDefaultAssets )
    {
        constexpr uint32 kDecimalDigitCount = 10;
        static_assert( kDeferredTileClassCount <= kDecimalDigitCount, "타일 종류 define 은 한 자리 숫자로 적는다" );
        RenderPassShaderSelection selection = selectRenderPassShader( lightingDesc._type, &lightingDesc, &lightingDesc, engineDefaultAssets );
        string                    classDefine{ "SW_DEFERRED_TILE_CLASS=" };
        classDefine.push_back( static_cast<char>( '0' + tileClass ) );
        selection._listDefine.push_back( std::move( classDefine ) );
        return selection;
    }

    bool isDiagnosticPassRequested( RenderPassKind type )
    {
        const RenderPassKindInfo& info = getRenderPassKindInfo( type );
        if ( info.hasFlag( RenderPassTraitFlag::kDiagnostic ) == false )
            return false;
        switch ( type )
        {
            case RenderPassKind::NanCheck:
                return RenderDiagnostics::get()._bNanCheck != SW_FALSE; // 배포본은 정책 기본값(끔)이라 PSO 도 디스패치도 없다
            default:
                return false;
        }
    }

    bool passAppliesTonemap( const RenderGraphPassDesc& passDesc )
    {
        if ( passDesc._type == RenderPassKind::Tonemap )
            return true;
        if ( passDesc._type != RenderPassKind::Present )
            return false;
        const string_view define{ kPostTonemapDefine };
        for ( const string& permutation : passDesc._listPermutation )
        {
            // "이름" 또는 "이름=값" — 이름 부분만 비교한다(post_chain.hlsl 은 `defined()` 로 본다).
            const string_view name = string_view{ permutation }.substr( 0, permutation.find( '=' ) );
            if ( name == define )
                return true;
        }
        return false;
    }
} // namespace sw
