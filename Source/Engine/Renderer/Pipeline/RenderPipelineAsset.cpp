#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPipelineAsset" );

    bool RenderPipelineAsset::loadFromXMLFile( const ResourceID& resourceID )
    {
        const string_view assetRelativePath = resourceID.view();
        _desc                               = {};
        if ( ReflectedXMLFile::loadDesc( resourceID, _desc ) == false )
            return false;
        return finishLoad( assetRelativePath );
    }

    bool RenderPipelineAsset::loadFromDiskFile( string_view diskPath )
    {
        _desc = {};
        if ( ReflectedXMLFile::loadDescFromDiskFile( diskPath, _desc ) == false )
            return false;
        return finishLoad( diskPath );
    }

    bool RenderPipelineAsset::finishLoad( string_view sourcePath )
    {
        // 검증 오류가 하나라도 있으면 로드 실패다. 선언하지 않은 첨부 · 모르는 패스 타입이 렌더러까지 가면 렌더러가 코드의 이름 · 포맷으로 짐작해야 한다.
        const uint32 issueCount = validate( sourcePath );
        if ( issueCount > 0 )
        {
            SW_LOG_ERROR( "'%#' has %# pipeline issue(s) - not loaded", sourcePath, issueCount );
            return false;
        }
        SW_LOG_INFO( "Loaded '%#' (model=%#, attachments=%#, passes=%#)",
                     _desc._name, _desc._shadingModel, _desc._listAttachment.size(), _desc._listPass.size() );
        return true;
    }

    namespace
    {
        /**
         * @brief 파이프라인 검증 규칙들입니다. 규칙마다 찾은 문제를 오류 로그로 남기고 그 수를 돌려줍니다.
         * @details `RenderPipelineAsset::validate` 가 순서대로 부르고 합을 냅니다. 패스의 해석 값(`_resolved*`)은 `resolveAndCheckPassAttachments` 가
         *          채우고, 그 뒤 규칙들이 그 값을 읽습니다 — 실행도 같은 해석을 쓴다.
         */
        struct RenderPipelineValidationInternal
        {
            static const RenderPassAttachment* findAttachment( const RenderPipelineDesc& desc, string_view name )
            {
                for ( const RenderPassAttachment& attachment : desc._listAttachment )
                {
                    if ( attachment._name == name )
                        return &attachment;
                }
                return nullptr;
            }

            static bool isDepthAttachment( const RenderPassAttachment* pAttachment )
            {
                return pAttachment != nullptr && engine::getSubsystem<TypeRegistry>().enumFromString<RHIFormat>( pAttachment->_format ) == constant::kDepthStencilFormat;
            }

            /** @brief 1) 첨부: 포맷 표기가 RHIFormat 으로 해석되는가, 렌더 타깃이 될 수 있는가, 이름이 겹치지 않는가, 나눗수 · 역할 표기가 맞는가. */
            static uint32 checkAttachments( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( uint32 attachmentIndex = 0; attachmentIndex < desc._listAttachment.size(); ++attachmentIndex )
                {
                    const RenderPassAttachment& attachment = desc._listAttachment[attachmentIndex];
                    RHIFormat                   parsed{};
                    if ( engine::getSubsystem<TypeRegistry>().enumFromString( attachment._format, parsed ) == false )
                    {
                        SW_LOG_ERROR( "[%#] attachment '%#': unknown format '%#'", sourcePath, attachment._name, attachment._format );
                        ++issueCount;
                    }
                    else if ( isRHIFormatRenderable( parsed ) == false )
                    {
                        // 이름은 맞지만 첨부가 될 수 없는 포맷(Unknown · 블록 압축 · R32G32B32_FLOAT) — 트랜지언트를 만들 때 백엔드가 실패한다.
                        SW_LOG_ERROR( "[%#] attachment '%#': format '%#' cannot be a render target", sourcePath, attachment._name, attachment._format );
                        ++issueCount;
                    }
                    for ( uint32 otherIndex = 0; otherIndex < attachmentIndex; ++otherIndex )
                    {
                        if ( desc._listAttachment[otherIndex]._name == attachment._name )
                        {
                            SW_LOG_ERROR( "[%#] attachment '%#' name is duplicated", sourcePath, attachment._name );
                            ++issueCount;
                            break;
                        }
                    }
                    if ( isSupportedResolutionDivisor( attachment._resolutionDivisor ) == false )
                    {
                        SW_LOG_ERROR( "[%#] attachment '%#': _resolutionDivisor %# - only 1, 2 and 4 are accepted", sourcePath, attachment._name, attachment._resolutionDivisor );
                        ++issueCount;
                    }
                    // 역할 선언은 아는 글이어야 한다. 모르는 글을 조용히 이름 · 포맷 규칙으로 넘기면 선언한 사람이 왜 안 걸리는지 모른다.
                    RenderPassInputRole declaredRole{ RenderPassInputRole::Invalid };
                    if ( attachment._role.empty() == false && tryParseRenderPassInputRole( attachment._role, declaredRole ) == false )
                    {
                        SW_LOG_ERROR( "[%#] attachment '%#': unknown role '%#' - must be one of SourceColor, SceneDepth, GBufferAlbedo, GBufferNormal, GBufferMaterial, "
                                      "ShadowMap, AmbientOcclusion, BloomBright, SceneColorCopy, SceneDepthCopy",
                                      sourcePath, attachment._name, attachment._role );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /** @brief 2-0) 패스 이름은 하나뿐이어야 한다 — 이름이 패스의 열쇠라 뒤의 것은 버려진다. */
            static uint32 checkPassNamesUnique( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    for ( size_t earlierIndex = 0; earlierIndex < passIndex; ++earlierIndex )
                    {
                        if ( desc._listPass[earlierIndex]._name == desc._listPass[passIndex]._name )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#' is declared twice - the name is the pass key, so the later one is dropped", sourcePath,
                                          desc._listPass[passIndex]._name );
                            ++issueCount;
                            break;
                        }
                    }
                }
                return issueCount;
            }

            /** @brief 패스 하나의 타입 · 뎁스 · 입출력 역할을 해석해 `_resolved*` 에 채웁니다. 역할은 첨부의 선언(`_role`)이 먼저, 그다음 원본 이름 · 포맷이다. */
            static void resolvePass( const RenderPipelineDesc& desc, RenderGraphPassDesc& pass )
            {
                pass._resolvedType = engine::getSubsystem<TypeRegistry>().enumFromString<RenderPassType>( pass._type );
                pass._resolvedDepthAttachment =
                    pass._depthAttachment.empty() ? hashed_string{} : hashed_string( pass._depthAttachment.c_str() );
                pass._listResolvedInput.clear();
                pass._listResolvedOutput.clear();
                pass._listResolvedColorOutput.clear();
                auto resolveAttachment = [&desc]( const string& attachmentName ) -> RenderGraphPassDesc::ResolvedAttachment
                {
                    const RenderPassAttachment*             pAttachment = findAttachment( desc, attachmentName );
                    RenderGraphPassDesc::ResolvedAttachment resolved{};
                    resolved._attachment = hashed_string( attachmentName.c_str() );
                    resolved._role       = static_cast<uint8>(
                        resolveRenderPassInputRole( attachmentName, isDepthAttachment( pAttachment ), pAttachment != nullptr ? string_view( pAttachment->_role ) : string_view{} ) );
                    return resolved;
                };
                for ( const string& inputName : pass._listInput )
                {
                    pass._listResolvedInput.push_back( resolveAttachment( inputName ) );
                }
                for ( const string& outputName : pass._listOutput )
                {
                    pass._listResolvedOutput.emplace_back( outputName.c_str() );
                    const RenderPassAttachment* pOutput = findAttachment( desc, outputName );
                    if ( pOutput != nullptr && isDepthAttachment( pOutput ) == false )
                        pass._listResolvedColorOutput.push_back( resolveAttachment( outputName ) );
                }
            }

            /** @brief 2-1) 패스 하나의 타입 표기가 해석되는가, 입출력이 선언된 첨부를 가리키는가. */
            static uint32 checkPassTypeAndAttachments( const RenderPipelineDesc& desc, const RenderGraphPassDesc& pass, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                if ( isPipelinePassType( pass._resolvedType ) == false )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': unknown type '%#' - not a RenderPassType spelling",
                                  sourcePath, pass._name, pass._type );
                    ++issueCount;
                }
                for ( const string& inputName : pass._listInput )
                {
                    if ( findAttachment( desc, inputName ) == nullptr )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': input '%#' is not in _attachments", sourcePath, pass._name, inputName );
                        ++issueCount;
                    }
                }
                for ( const string& outputName : pass._listOutput )
                {
                    // 스왑체인은 파이프라인이 선언하는 첨부가 아니라 디바이스가 주는 백버퍼다. 포맷이 대상마다 달라 PSO 를 대상 포맷마다 만드는
                    // 출력 패스(kOutputPass)만 쓸 수 있다 — 다른 패스가 쓰면 PSO 의 렌더 타깃 포맷을 정할 근거가 없다(짐작하지 않는다).
                    if ( outputName == kSwapchainOutputName )
                    {
                        if ( getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kOutputPass ) == false )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#': only output passes (Present, Canvas) can write the Swapchain (type '%#')", sourcePath, pass._name, pass._type );
                            ++issueCount;
                        }
                        continue;
                    }
                    if ( findAttachment( desc, outputName ) == nullptr )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': output '%#' is not in _attachments", sourcePath, pass._name, outputName );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 2-2) 뎁스 첨부: 비어 있는 것은 "일부러 뎁스를 안 쓴다" 는 뜻이라 정상이다. 다만 이름을 적었으면 그 첨부가 실재하고 뎁스 포맷이어야 한다.
             * @details 컬러 첨부를 뎁스로 바인딩하면 렌더 패스가 통째로 비호환이 된다. 뎁스를 출력에 적어 놓고 바인딩하지 않는 것도 앞뒤가 맞지 않는다.
             */
            static uint32 checkPassDepth( const RenderPipelineDesc& desc, const RenderGraphPassDesc& pass, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                if ( pass._depthAttachment.empty() == false )
                {
                    const RenderPassAttachment* pDepthAttachment = findAttachment( desc, pass._depthAttachment );
                    if ( pDepthAttachment == nullptr )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': depth attachment '%#' is not in _attachments",
                                      sourcePath, pass._name, pass._depthAttachment );
                        ++issueCount;
                    }
                    else if ( isDepthAttachment( pDepthAttachment ) == false )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': depth attachment '%#' has format '%#' - it must be a depth format",
                                      sourcePath, pass._name, pass._depthAttachment, pDepthAttachment->_format );
                        ++issueCount;
                    }
                    return issueCount;
                }
                for ( const string& outputName : pass._listOutput )
                {
                    if ( isDepthAttachment( findAttachment( desc, outputName ) ) == false )
                        continue;
                    SW_LOG_ERROR( "[%#] pass '%#': depth attachment '%#' is declared as an output but _depthAttachment is empty "
                                  "- as is, it draws without depth",
                                  sourcePath, pass._name, outputName );
                    ++issueCount;
                }
                return issueCount;
            }

            /** @brief 2) 패스마다 해석 값을 채우고 타입 · 입출력 · 뎁스를 검사합니다. */
            static uint32 resolveAndCheckPassAttachments( RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( RenderGraphPassDesc& pass : desc._listPass )
                {
                    resolvePass( desc, pass );
                    issueCount += checkPassTypeAndAttachments( desc, pass, sourcePath );
                    issueCount += checkPassDepth( desc, pass, sourcePath );
                }
                return issueCount;
            }

            /** @brief 2-2) 모듈 셰이더 목록의 경로는 비지 않고 겹치지 않아야 한다 — 쿠커가 경로마다 한 번 굽고, 런타임이 이 목록으로 모듈 패스의 프로그램을 거른다. */
            static uint32 checkModuleShaderList( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t shaderIndex = 0; shaderIndex < desc._listModuleShader.size(); ++shaderIndex )
                {
                    if ( desc._listModuleShader[shaderIndex].empty() )
                    {
                        SW_LOG_ERROR( "[%#] _listModuleShader[%#] is empty", sourcePath, static_cast<uint32>( shaderIndex ) );
                        ++issueCount;
                        continue;
                    }
                    for ( size_t earlierIndex = 0; earlierIndex < shaderIndex; ++earlierIndex )
                    {
                        if ( desc._listModuleShader[earlierIndex] == desc._listModuleShader[shaderIndex] )
                        {
                            SW_LOG_ERROR( "[%#] _listModuleShader lists '%#' twice", sourcePath, desc._listModuleShader[shaderIndex] );
                            ++issueCount;
                            break;
                        }
                    }
                }
                return issueCount;
            }

            /** @brief 3) 컬러 첨부가 여러 개인 패스는 MRT 로 묶인다. 포맷은 서로 달라도 되지만 개수에는 한계가 있다. */
            static uint32 checkColorOutputLimit( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    uint32 colorCount{ 0 };
                    for ( const string& outputName : pass._listOutput )
                    {
                        const RenderPassAttachment* pAttachment = findAttachment( desc, outputName );
                        if ( pAttachment == nullptr )
                            continue;
                        RHIFormat fmt{};
                        if ( engine::getSubsystem<TypeRegistry>().enumFromString( pAttachment->_format, fmt ) && fmt != RHIFormat::D24_UNORM_S8_UINT )
                            ++colorCount;
                    }
                    if ( colorCount > kMaxColorAttachments )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': %# color outputs exceed the limit (%#)",
                                      sourcePath, pass._name, colorCount, static_cast<uint32>( kMaxColorAttachments ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-0) 한 패스의 출력(컬러 · 깊이)은 크기가 같아야 한다 — 렌더 패스의 타깃은 한 크기다.
             * @details 나눗수가 다르면 D3D 는 렌더 패스를 거부하고 Vulkan 은 프레임버퍼를 못 만든다.
             */
            static uint32 checkOutputDivisors( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const RenderPassAttachment* pFirst           = nullptr;
                    auto                        checkSameDivisor = [&]( string_view attachmentName )
                    {
                        const RenderPassAttachment* pAttachment = findAttachment( desc, attachmentName );
                        if ( pAttachment == nullptr )
                            return;
                        if ( pFirst == nullptr )
                        {
                            pFirst = pAttachment;
                            return;
                        }
                        if ( pAttachment->_resolutionDivisor == pFirst->_resolutionDivisor )
                            return;
                        SW_LOG_ERROR( "[%#] pass '%#': outputs '%#' (divisor %#) and '%#' (divisor %#) differ in size - all outputs of one pass must have the same size",
                                      sourcePath, pass._name, pFirst->_name, pFirst->_resolutionDivisor, pAttachment->_name, pAttachment->_resolutionDivisor );
                        ++issueCount;
                    };
                    for ( const string& outputName : pass._listOutput )
                    {
                        checkSameDivisor( outputName );
                    }
                    if ( pass._depthAttachment.empty() == false )
                        checkSameDivisor( pass._depthAttachment );
                }
                return issueCount;
            }

            /**
             * @brief 3-1 · 3-2) 컬러를 그리는 패스(kDrawsDeclaredColor)는 선언한 컬러 출력에 그리고, G버퍼는 알베도 · 노멀 · 머티리얼을 한 MRT 패스로 쓴다.
             * @details 컬러 출력이 없으면 그릴 곳이 없다(없는 첨부의 핸들 0 은 백버퍼라, 그대로 두면 화면에 그린다). G버퍼의 컬러 수가 표의 값과 다르면
             *          PSO 와 렌더 패스가 어긋나거나, 노멀이 없어 Lighting · SSAO 가 읽을 것이 없다.
             */
            static uint32 checkGeometryColorOutputs( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32       issueCount{ 0 };
                const uint32 expectedGBufferColorCount = getRenderPassTypeInfo( RenderPassType::GBuffer )._colorTargetCount;
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    // 컬러를 그리는 패스(표의 kDrawsDeclaredColor — 지오메트리 · 하늘 · 외곽선 껍질 · TAA · 일반 풀스크린)는 선언한 컬러 출력에 그린다.
                    // 실행은 이름으로 타깃을 짐작하지 않으므로 선언이 없으면 그릴 곳이 없다(옛 짐작은 없는 첨부의 핸들 0 — 백버퍼 — 를 열었다).
                    const bool bDrawsColor = isPipelinePassType( pass._resolvedType ) &&
                                             getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kDrawsDeclaredColor );
                    if ( bDrawsColor && pass._listResolvedColorOutput.empty() )
                    {
                        SW_LOG_ERROR_ID( "Renderer.PassNoColorOutput", "[%#] pass '%#' (%#): no color output - this pass draws to the color attachments declared in _listOutput",
                                         sourcePath, pass._name, pass._type );
                        ++issueCount;
                    }
                    const bool bSkipGBufferOutputCheck = pass._resolvedType != RenderPassType::GBuffer || pass._listResolvedColorOutput.empty() ||
                                                         pass._listResolvedColorOutput.size() == expectedGBufferColorCount;
                    if ( bSkipGBufferOutputCheck )
                        continue;
                    SW_LOG_ERROR( "[%#] pass '%#' (%#): %# color outputs - the G-buffer writes albedo, normal and material (%#) in one MRT pass",
                                  sourcePath, pass._name, pass._type, static_cast<uint32>( pass._listResolvedColorOutput.size() ), expectedGBufferColorCount );
                    ++issueCount;
                }
                return issueCount;
            }

            /**
             * @brief 4) 풀스크린 패스의 입력이 그 타입의 계약과 맞는가. "선언만 있고 아무도 안 읽는 입력" 을 여기서 잡는다.
             * @details 그런 입력은 오류 없이 지나가고, 그것을 만드는 패스(예: SSAO)만 매 프레임 돌고 버려진다. 실행은 같은 해석(_listResolvedInput)을 그대로 건다.
             */
            static uint32 checkInputContracts( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const RenderPassInputSignature* pContract = findRenderPassInputSignature( pass._resolvedType );
                    if ( pContract == nullptr )
                        continue;

                    uint32 sourceColorCount{ 0 };
                    // 에뮬 백엔드(DX11 · GL)는 한 슬롯을 여러 역할이 나눠 쓴다(getEmulatedTextureSlot). 같은 슬롯의 두 역할을 함께 읽으면 실행이 하나만 걸어
                    // 다른 하나가 조용히 빠지므로 로드에서 거절한다.
                    RenderPassInputRole arrSlotRole[static_cast<uint32>( EmulatedTextureSlot::None )]{};
                    for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                    {
                        const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
                        if ( role == RenderPassInputRole::SourceColor )
                            ++sourceColorCount;
                        const EmulatedTextureSlot slot = getEmulatedTextureSlot( role );
                        if ( slot != EmulatedTextureSlot::None )
                        {
                            RenderPassInputRole& slotRole = arrSlotRole[static_cast<uint32>( slot )];
                            if ( slotRole != RenderPassInputRole::Invalid && slotRole != role )
                            {
                                SW_LOG_ERROR( "[%#] pass '%#' (%#): inputs with roles %# and %# share emulated texture slot %# (DX11 · GL) - only one can be bound",
                                              sourcePath, pass._name, pass._type, getRenderPassInputRoleName( slotRole ), getRenderPassInputRoleName( role ),
                                              static_cast<uint32>( slot ) );
                                ++issueCount;
                            }
                            slotRole = role;
                        }
                        if ( pContract->reads( role ) )
                            continue;
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): input '%#' (role %#) is not read by this pass type - it is declared but never bound",
                                      sourcePath, pass._name, pass._type, input._attachment.c_str(), getRenderPassInputRoleName( role ) );
                        ++issueCount;
                    }
                    if ( sourceColorCount > 1 )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): %# color inputs to process (SourceColor role) - the shader reads only one",
                                      sourcePath, pass._name, pass._type, sourceColorCount );
                        ++issueCount;
                    }
                    for ( uint32 requiredIndex = 0; requiredIndex < pContract->_requiredCount; ++requiredIndex )
                    {
                        const RenderPassInputRole required = pContract->_arrRequired[requiredIndex];
                        const bool                bFound   = std::any_of( pass._listResolvedInput.begin(), pass._listResolvedInput.end(),
                                                                          [required]( const RenderGraphPassDesc::ResolvedAttachment& input )
                                         { return static_cast<RenderPassInputRole>( input._role ) == required; } );
                        if ( bFound )
                            continue;
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): required input (role %#) is not declared - the shader reads kInvalidIndex",
                                      sourcePath, pass._name, pass._type, getRenderPassInputRoleName( required ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-3 · 3-4) Canvas(화면 2D)는 주 출력(Swapchain)에 그리고 Swapchain 을 쓰는 **마지막** 패스여야 하며, 하늘은 뎁스 첨부가 있어야 한다.
             * @details 뒤에 Swapchain 을 쓰는 패스가 있으면 UI 가 덮이고, 스크린샷 캡처 → 백버퍼 복사(마지막 Swapchain 쓰기 패스 끝)가 UI 앞에서 일어난다.
             *          UI 가 없는 파이프라인(Canvas 없음)은 허용한다. 하늘은 깊이 테스트로 빈 곳만 칠한다 — 뎁스 첨부가 없으면 장면 위를 통째로 덮는다.
             */
            static uint32 checkCanvasAndSky( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32                     issueCount{ 0 };
                const RenderGraphPassDesc* pCanvas = nullptr;
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const bool bWritesSwapchain = VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) );
                    if ( pass._resolvedType == RenderPassType::Canvas )
                    {
                        if ( bWritesSwapchain == false )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#' (Canvas): the output is not the Swapchain - Canvas draws to the main output", sourcePath, pass._name );
                            ++issueCount;
                        }
                        pCanvas = &pass;
                        continue;
                    }
                    if ( pCanvas != nullptr && bWritesSwapchain )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': writes the Swapchain after Canvas '%#' - Canvas must be the last pass that writes the Swapchain (the UI would be covered)",
                                      sourcePath, pass._name, pCanvas->_name );
                        ++issueCount;
                    }
                }
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    if ( pass._resolvedType != RenderPassType::Sky || pass._depthAttachment.empty() == false )
                        continue;
                    SW_LOG_ERROR( "[%#] pass '%#' (Sky): _depthAttachment is empty - the sky paints only where nothing was drawn, using the depth test", sourcePath, pass._name );
                    ++issueCount;
                }
                return issueCount;
            }

            /**
             * @brief 3-5) 후처리 품질로 건너뛰는 패스(`_minPostQuality`)는 후처리를 끈 뷰처럼 건너뛸 수 있는 타입(kSkipsWithoutPostProcess — 원본을 넘기거나 기본 클리어)만이다.
             * @details 다른 타입을 건너뛰면 출력 첨부에 지난 프레임 값이 남는다.
             */
            static uint32 checkMinPostQuality( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const bool bSkippable = getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess );
                    if ( pass._minPostQuality < 0 )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#': _minPostQuality %# - must be 0 or greater", sourcePath, pass._name, pass._minPostQuality );
                        ++issueCount;
                    }
                    else if ( pass._minPostQuality > 0 && bSkippable == false )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): _minPostQuality is accepted only for types whose post-processing can be skipped (SSAO, Bloom, Outline, MotionBlur)", sourcePath, pass._name, pass._type );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-6) 꺼지면 원본을 가리키는 패스(kBypassesToSource)는 원본(SourceColor 입력) 하나와 컬러 출력 하나를 갖고, 둘은 형식 · 나눗수가 같다.
             *        그 출력을 쓰는 패스는 자기 하나이고, 뒤의 패스가 원본을 다시 쓰지 않는다.
             * @details 꺼진 뷰에서는 출력 이름이 원본 텍스처를 가리킨다(`TransientAttachmentPool::forward`). 형식 · 크기가 다르면 읽는 쪽 PSO · 텍셀 크기와 어긋나고,
             *          출력을 다른 패스도 쓰면 그 쓰기가 원본을 덮고, 뒤에서 원본을 쓰면 출력을 읽는 쪽이 나중 판을 읽는다.
             */
            static uint32 checkBypassPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listPass[passIndex];
                    if ( getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kBypassesToSource ) == false )
                        continue;
                    const RenderGraphPassDesc::ResolvedAttachment* pSource     = nullptr;
                    uint32                                         sourceCount = 0;
                    for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                    {
                        if ( static_cast<RenderPassInputRole>( input._role ) != RenderPassInputRole::SourceColor )
                            continue;
                        pSource = &input;
                        ++sourceCount;
                    }
                    if ( sourceCount != 1 )
                        continue; // 원본이 없거나 둘이면 입력 계약 검사(4)가 이미 알렸다(이 타입들은 원본이 필수 입력이다)
                    if ( pass._listResolvedColorOutput.empty() )
                        continue; // 컬러 출력이 없으면 3-1 · 3-2 가 이미 알렸다(이 타입들은 모두 kDrawsDeclaredColor 다)
                    if ( pass._listResolvedColorOutput.size() != 1 )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): needs one color output - when the effect is off the output names the source",
                                      sourcePath, pass._name, pass._type );
                        ++issueCount;
                        continue;
                    }
                    const string                outputName = pass._listResolvedColorOutput[0]._attachment.c_str();
                    const string                sourceName = pSource->_attachment.c_str();
                    const RenderPassAttachment* pOutput    = findAttachment( desc, outputName );
                    const RenderPassAttachment* pSourceAtt = findAttachment( desc, sourceName );
                    const bool                  bSameShape = pOutput != nullptr && pSourceAtt != nullptr && pOutput->_format == pSourceAtt->_format && pOutput->_resolutionDivisor == pSourceAtt->_resolutionDivisor;
                    if ( bSameShape == false )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): output '%#' and source '%#' differ in format or divisor - when the effect is off the output names the source",
                                      sourcePath, pass._name, pass._type, outputName, sourceName );
                        ++issueCount;
                    }
                    for ( size_t otherIndex = 0; otherIndex < desc._listPass.size(); ++otherIndex )
                    {
                        const RenderGraphPassDesc& other = desc._listPass[otherIndex];
                        if ( otherIndex != passIndex && VectorUtil::contains( other._listOutput, outputName ) )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#' also writes '%#', the output of '%#' (%#) - that output may name the source texture", sourcePath, other._name, outputName,
                                          pass._name, pass._type );
                            ++issueCount;
                        }
                        if ( otherIndex > passIndex && VectorUtil::contains( other._listOutput, sourceName ) )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#' writes '%#' after '%#' (%#) reads it - readers of '%#' would see the later version when the effect is off", sourcePath,
                                          other._name, sourceName, pass._name, pass._type, outputName );
                            ++issueCount;
                        }
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-7) 꺼지면 출력이 없는 패스(kOmittedWhenOff)는 컬러 출력 하나를 혼자 쓰고, 그 출력을 읽는 패스는 모두 그 역할을 선택 입력으로만 받는다.
             * @details 꺼진 뷰에서는 출력 이름이 아무 첨부도 가리키지 않는다(`TransientAttachmentPool::markAbsent`). 읽는 쪽은 그 역할을 바인딩하지 않아 셰이더 기본값(AO = 1)을 쓴다 —
             *          필수 입력이거나 계약이 없는 패스가 읽으면 그 기본값이 없다.
             */
            static uint32 checkOmittedPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listPass[passIndex];
                    if ( getRenderPassTypeInfo( pass._resolvedType ).hasFlag( RenderPassTraitFlag::kOmittedWhenOff ) == false )
                        continue;
                    if ( pass._listResolvedColorOutput.size() != 1 )
                    {
                        SW_LOG_ERROR( "[%#] pass '%#' (%#): needs one color output - when the effect is off that output is absent", sourcePath, pass._name, pass._type );
                        ++issueCount;
                        continue;
                    }
                    const hashed_string& output     = pass._listResolvedColorOutput[0]._attachment;
                    const string         outputName = output.c_str();
                    for ( size_t otherIndex = 0; otherIndex < desc._listPass.size(); ++otherIndex )
                    {
                        if ( otherIndex == passIndex )
                            continue;
                        const RenderGraphPassDesc& other = desc._listPass[otherIndex];
                        if ( VectorUtil::contains( other._listOutput, outputName ) )
                        {
                            SW_LOG_ERROR( "[%#] pass '%#' also writes '%#', the output of '%#' (%#) - that output may be absent", sourcePath, other._name, outputName, pass._name,
                                          pass._type );
                            ++issueCount;
                        }
                        const RenderPassInputSignature* pContract = findRenderPassInputSignature( other._resolvedType );
                        for ( const RenderGraphPassDesc::ResolvedAttachment& input : other._listResolvedInput )
                        {
                            if ( input._attachment != output )
                                continue;
                            const RenderPassInputRole role      = static_cast<RenderPassInputRole>( input._role );
                            bool                      bRequired = false;
                            for ( uint32 requiredIndex = 0; pContract != nullptr && requiredIndex < pContract->_requiredCount; ++requiredIndex )
                            {
                                bRequired = bRequired || pContract->_arrRequired[requiredIndex] == role;
                            }
                            const bool bOptionalRead = pContract != nullptr && pContract->reads( role ) && bRequired == false;
                            if ( bOptionalRead )
                                continue;
                            SW_LOG_ERROR( "[%#] pass '%#' reads '%#' (role %#) not as an optional input - when '%#' (%#) is off that input is absent", sourcePath, other._name,
                                          outputName, getRenderPassInputRoleName( role ), pass._name, pass._type );
                            ++issueCount;
                        }
                    }
                }
                return issueCount;
            }
        };
    } // namespace

    uint32 RenderPipelineAsset::validate( string_view sourcePath )
    {
        using Internal = RenderPipelineValidationInternal;
        // 순서가 있다 — 패스 해석(2)이 채운 `_resolved*` 를 뒤 규칙들이 읽는다.
        uint32 issueCount = Internal::checkAttachments( _desc, sourcePath );
        issueCount += Internal::checkPassNamesUnique( _desc, sourcePath );
        issueCount += Internal::resolveAndCheckPassAttachments( _desc, sourcePath );
        issueCount += Internal::checkModuleShaderList( _desc, sourcePath );
        issueCount += Internal::checkColorOutputLimit( _desc, sourcePath );
        issueCount += Internal::checkOutputDivisors( _desc, sourcePath );
        issueCount += Internal::checkGeometryColorOutputs( _desc, sourcePath );
        issueCount += Internal::checkInputContracts( _desc, sourcePath );
        issueCount += Internal::checkCanvasAndSky( _desc, sourcePath );
        issueCount += Internal::checkMinPostQuality( _desc, sourcePath );
        issueCount += Internal::checkBypassPasses( _desc, sourcePath );
        issueCount += Internal::checkOmittedPasses( _desc, sourcePath );
        if ( issueCount > 0 )
            SW_LOG_ERROR( "[%#] Pipeline validation found %# issues - the render result may be wrong or the GPU may die",
                          sourcePath, issueCount );
        return issueCount;
    }

    bool RenderPipelineAsset::saveToXMLFile( string_view assetRelativePath ) const
    {
        if ( ReflectedXMLFile::saveDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Saved '%#' -> %#", _desc._name, assetRelativePath );
        return true;
    }

    TaskHandle RenderPipelineAsset::loadFromXMLFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getSubsystem<TaskManager>().emplaceTask(
            "LoadRenderPipelineAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPipelineAsset::loadFromXMLFileAsyncJob ),
            MakeTaskArgs( this, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void RenderPipelineAsset::loadFromXMLFileAsyncJob( const TaskArgs& args )
    {
        RenderPipelineAsset* pResource = args.get<RenderPipelineAsset*>( 0 );
        if ( pResource == nullptr )
            return;
        if ( pResource->loadFromXMLFile( ResourceID{ args.get<string>( 1 ) } ) == false )
            SW_LOG_WARNING( "Could not load '%#'", args.get<string>( 1 ) );
    }

} // namespace sw
