#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingValidator.h"

#include "Core/Common/Defines.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingValidator" );

    namespace
    {
        struct ShaderBindingValidatorInternal
        {
            static inline atomic<uint32> s_totalViolation{ 0 };

            /// @brief 한 리소스를 이름공간 키로 만들 때 쓰는 레지스터 종류입니다(DX 기준).
            enum class RegisterClass : uint8
            {
                ConstantBuffer,  // b
                ShaderResource,  // t
                UnorderedAccess, // u
                Sampler,         // s
                Other
            };

            /// @brief GL 이름공간입니다. GL 은 set 을 버리고 binding 만 보므로 종류별로 번호 공간이 갈립니다.
            enum class GlNamespace : uint8
            {
                UniformBuffer,
                TextureUnit,
                StorageBuffer,
                Image,
                Other
            };

            static RegisterClass registerClassOf( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return RegisterClass::ConstantBuffer;
                    case ShaderBindingKind::Texture:
                    case ShaderBindingKind::StructuredBuffer:
                        return RegisterClass::ShaderResource;
                    case ShaderBindingKind::RwStructuredBuffer:
                    case ShaderBindingKind::RwTexture:
                        return RegisterClass::UnorderedAccess;
                    case ShaderBindingKind::Sampler:
                        return RegisterClass::Sampler;
                    case ShaderBindingKind::Unknown:
                        return RegisterClass::Other;
                }
            }

            static GlNamespace glNamespaceOf( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return GlNamespace::UniformBuffer;
                    case ShaderBindingKind::Texture:
                    case ShaderBindingKind::Sampler:
                        return GlNamespace::TextureUnit;
                    case ShaderBindingKind::StructuredBuffer:
                    case ShaderBindingKind::RwStructuredBuffer:
                        return GlNamespace::StorageBuffer;
                    case ShaderBindingKind::RwTexture:
                        return GlNamespace::Image;
                    case ShaderBindingKind::Unknown:
                        return GlNamespace::Other;
                }
            }

            static const utf8* registerClassLetter( RegisterClass cls )
            {
                switch ( cls )
                {
                    case RegisterClass::ConstantBuffer:
                        return "b";
                    case RegisterClass::ShaderResource:
                        return "t";
                    case RegisterClass::UnorderedAccess:
                        return "u";
                    case RegisterClass::Sampler:
                        return "s";
                    case RegisterClass::Other:
                        return "?";
                }
            }

            static const utf8* glNamespaceName( GlNamespace glNamespace )
            {
                switch ( glNamespace )
                {
                    case GlNamespace::UniformBuffer:
                        return "UBO";
                    case GlNamespace::TextureUnit:
                        return "texture unit";
                    case GlNamespace::StorageBuffer:
                        return "SSBO";
                    case GlNamespace::Image:
                        return "image unit";
                    case GlNamespace::Other:
                        return "?";
                }
            }

            static const utf8* kindName( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return "ConstantBuffer";
                    case ShaderBindingKind::Texture:
                        return "Texture";
                    case ShaderBindingKind::Sampler:
                        return "Sampler";
                    case ShaderBindingKind::StructuredBuffer:
                        return "StructuredBuffer";
                    case ShaderBindingKind::RwStructuredBuffer:
                        return "RwStructuredBuffer";
                    case ShaderBindingKind::RwTexture:
                        return "RwTexture";
                    case ShaderBindingKind::Unknown:
                        return "Unknown";
                }
            }

            static bool isSPIRV( ShaderTargetFormat target )
            {
                return target == ShaderTargetFormat::SPIRV_Vulkan || target == ShaderTargetFormat::SPIRV_OpenGL;
            }

            /**
             * @brief 종류가 계약과 "같다" 고 볼 수 있는지 반환합니다.
             * @details SPIR-V 는 읽기/쓰기 구조버퍼를 모두 StorageBuffer 로만 보고하고, 결합 이미지 샘플러는
             *          텍스처와 샘플러를 하나로 보고합니다. 그 차이는 어긋남이 아닙니다.
             */
            static bool kindMatches( ShaderBindingKind expected, ShaderBindingKind actual, ShaderTargetFormat target )
            {
                if ( expected == actual )
                    return true;
                if ( isSPIRV( target ) )
                {
                    const bool bExpectedStorage = ( expected == ShaderBindingKind::StructuredBuffer || expected == ShaderBindingKind::RwStructuredBuffer );
                    const bool bActualStorage   = ( actual == ShaderBindingKind::StructuredBuffer || actual == ShaderBindingKind::RwStructuredBuffer );
                    if ( bExpectedStorage && bActualStorage )
                        return true;
                    const bool bExpectedImage = ( expected == ShaderBindingKind::Texture || expected == ShaderBindingKind::Sampler );
                    const bool bActualImage   = ( actual == ShaderBindingKind::Texture || actual == ShaderBindingKind::Sampler );
                    if ( bExpectedImage && bActualImage )
                        return true;
                }
                return false;
            }

            /**
             * @brief `[shift, shift + width)` 안에 드는지 반환합니다.
             * @details 부호 없는 뺄셈이라 `binding < shift` 면 아주 큰 값으로 감겨 width 를 넘습니다.
             *          그래서 하한 비교가 따로 필요 없습니다(b 범위의 shift 는 0 이라 `binding >= 0` 은 늘 참이고,
             *          컴파일러가 그것을 경고합니다).
             */
            static bool isInRange( uint32 binding, uint32 shift, uint32 width ) { return ( binding - shift ) < width; }

            /// @brief Vulkan 세트 0 binding 이 어느 레지스터 범위(b/t/u)인지 반환합니다. 범위 밖이면 Other 입니다.
            static RegisterClass vulkanRangeClassOf( uint32 binding )
            {
                namespace vk = shaderslot::vk;
                if ( isInRange( binding, vk::kBShift, vk::kRangeSize ) )
                    return RegisterClass::ConstantBuffer;
                if ( isInRange( binding, vk::kTShift, vk::kRangeSize ) )
                    return RegisterClass::ShaderResource;
                if ( isInRange( binding, vk::kUShift, vk::kRangeSize ) )
                    return RegisterClass::UnorderedAccess;
                return RegisterClass::Other;
            }

            /// @brief SPIR-V 는 읽기/쓰기 구조버퍼를 구분하지 않으므로 t/u 범위 모두 StorageBuffer 를 받습니다.
            static bool vulkanRangeAcceptsKind( RegisterClass rangeClass, ShaderBindingKind kind )
            {
                switch ( rangeClass )
                {
                    case RegisterClass::ConstantBuffer:
                        return kind == ShaderBindingKind::ConstantBuffer;
                    case RegisterClass::ShaderResource:
                    case RegisterClass::UnorderedAccess:
                        return kind == ShaderBindingKind::StructuredBuffer || kind == ShaderBindingKind::RwStructuredBuffer;
                    case RegisterClass::Sampler:
                    case RegisterClass::Other:
                        return false;
                }
            }

            struct ReflectedBinding
            {
                string            _name;
                ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
                uint32            _space{ 0 };
                uint32            _bindPoint{ 0 };
                uint32            _bindCount{ kUnknownCount }; ///< 리소스 목록에서 왔으면 배열 크기(무제한=0), CB 목록만 있으면 모름

                static constexpr uint32 kUnknownCount = invalid_index::kUint32;
            };

            /// @brief CB 목록과 리소스 목록을 (이름, 종류)로 중복 없이 합칩니다. DX 리플렉션은 cbuffer 를 양쪽에 다 넣습니다.
            static void collect( const ShaderReflectionData& reflection, vector<ReflectedBinding>& outList )
            {
                // 리소스 목록이 바인딩 위치의 1차 출처다(cbuffer 도 여기 들어 있다). CB 목록은 그 다음이다. 리플렉터가
                // CB 쪽 bindPoint 를 못 채우는 경우가 있다(DXIL, move 뒤 이름 비교).
                for ( const ShaderReflectedBinding& res : reflection._listResource )
                {
                    const ShaderBindingKind kind = ShaderBindingLayout::kindFromTypeLabel( static_cast<string_view>( res._type ) );
                    pushUnique( outList, res._name, kind, res._registerSpace, res._bindPoint, res._bindCount );
                }
                for ( const ShaderBufferInfo& cb : reflection._listConstantBuffer )
                {
                    pushUnique( outList, cb._name, ShaderBindingKind::ConstantBuffer, cb._registerSpace, cb._bindPoint, ReflectedBinding::kUnknownCount );
                }
            }

            /// @brief 같은 (이름, 종류)가 이미 있으면 넣지 않습니다.
            static void pushUnique( vector<ReflectedBinding>& outList, const string& name, ShaderBindingKind kind, uint32 space, uint32 bindPoint, uint32 bindCount )
            {
                for ( const ReflectedBinding& reflected : outList )
                {
                    if ( reflected._name == name && reflected._kind == kind )
                        return;
                }
                ReflectedBinding reflected{};
                reflected._name      = name;
                reflected._kind      = kind;
                reflected._space     = space;
                reflected._bindPoint = bindPoint;
                reflected._bindCount = bindCount;
                outList.push_back( std::move( reflected ) );
            }

            static void report( vector<ShaderBindingValidatorIssue>* pOutIssue, string_view shaderLabel, const string& resource, string&& message, uint32& ioCount )
            {
                ++ioCount;
                s_totalViolation.fetch_add( 1, std::memory_order_relaxed );
                SW_LOG_ERROR( "[Binding contract] %# - %#: %#", string( shaderLabel ).c_str(), resource.c_str(), message.c_str() );
                if ( pOutIssue != nullptr )
                {
                    ShaderBindingValidatorIssue issue{};
                    issue._resource = resource;
                    issue._message  = std::move( message );
                    pOutIssue->push_back( std::move( issue ) );
                }
            }

            static string formatLocation( const utf8* pFormat, uint32 space, uint32 bindPoint )
            {
                return string( pFormat ) + "(" + to_string( space ) + ", " + to_string( bindPoint ) + ")";
            }
        };

        ShaderReservedLocation at( uint32 space, uint32 bind )
        {
            ShaderReservedLocation location{};
            location._space     = space;
            location._bind      = bind;
            location._bDeclared = true;
            return location;
        }

        ShaderReservedLocation none()
        {
            return ShaderReservedLocation{};
        }

        /// @brief 계약 표에 항목 하나를 더합니다.
        void addReserved( vector<ShaderReservedBinding>& list, const utf8* pName, ShaderBindingKind kind, ShaderReservedLocation dx11,
                          ShaderReservedLocation dx12, ShaderReservedLocation vulkan, ShaderReservedLocation opengl )
        {
            ShaderReservedBinding entry{};
            entry._name   = pName;
            entry._kind   = kind;
            entry._dx11   = dx11;
            entry._dx12   = dx12;
            entry._vulkan = vulkan;
            entry._opengl = opengl;
            list.push_back( entry );
        }

        /// @brief 세트 0 의 (shift + reg) 자리입니다. 슬롯 리소스는 DX11 · DX12 · GL 이 (space 0, register) 로 같아 shift 가 0 이고,
        ///        Vulkan 은 b / t / u 범위마다 시프트(`shaderslot::vk::kBShift` 등)를 더합니다.
        ShaderReservedLocation slotAt( uint32 shift, uint32 reg )
        {
            return at( 0, shift + reg );
        }

        /// @brief 계약 표를 짓습니다. 값은 모두 shaderslot 에서 옵니다. 백엔드가 선언하지 않는 자리는 none() 입니다. 줄마다 리소스 하나인 등록 목록이라 나누지 않는다.
        vector<ShaderReservedBinding> buildReservedBindings()
        {
            namespace vk = shaderslot::vk;
            vector<ShaderReservedBinding> list;
            // 슬롯 리소스는 DX11 · DX12 · GL 이 (space 0, register) 로 같고, Vulkan 만 세트 0 의 시프트된 binding 이다.

            // 상수버퍼: b0 / b1
            addReserved( list, shaderslot::cbname::kPass, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kPassConstantBuffer ), slotAt( 0, shaderslot::kPassConstantBuffer ), slotAt( vk::kBShift, shaderslot::kPassConstantBuffer ), slotAt( 0, shaderslot::kPassConstantBuffer ) );
            addReserved( list, shaderslot::cbname::kMaterial, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kMaterialConstantBuffer ), slotAt( 0, shaderslot::kMaterialConstantBuffer ), slotAt( vk::kBShift, shaderslot::kMaterialConstantBuffer ), slotAt( 0, shaderslot::kMaterialConstantBuffer ) );
            addReserved( list, shaderslot::cbname::kCull, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( vk::kBShift, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ) );
            addReserved( list, shaderslot::cbname::kSort, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( vk::kBShift, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ) );
            addReserved( list, shaderslot::cbname::kAnimation, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ), slotAt( vk::kBShift, shaderslot::kComputeConstantBuffer ), slotAt( 0, shaderslot::kComputeConstantBuffer ) );
            // 루트/푸시 상수 블록: DX12 b0 space2, DX11 · GL b2 에뮬. Vulkan 은 푸시 상수라 바인딩 자리가 없다(리플렉션에 안 나온다).
            addReserved( list, shaderslot::cbname::kRootConstants, ShaderBindingKind::ConstantBuffer,
                         slotAt( 0, shaderslot::kRootConstantEmulationSlot ), at( shaderslot::kRootConstantSpace, shaderslot::kRootConstantRegister ), none(), slotAt( 0, shaderslot::kRootConstantEmulationSlot ) );
            // GPUScene 버퍼: 인스턴스 t4, 머티리얼 데이터 t5 (네 백엔드 공통)
            addReserved( list, shaderslot::resname::kInstances, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kInstanceBuffer ), slotAt( 0, shaderslot::kInstanceBuffer ), slotAt( vk::kTShift, shaderslot::kInstanceBuffer ), slotAt( 0, shaderslot::kInstanceBuffer ) );
            addReserved( list, shaderslot::resname::kMaterials, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kMaterialBuffer ), slotAt( 0, shaderslot::kMaterialBuffer ), slotAt( vk::kTShift, shaderslot::kMaterialBuffer ), slotAt( 0, shaderslot::kMaterialBuffer ) );
            // 컬링이 만든 가시 인스턴스 ID 목록: 그래픽스 t6 (네 백엔드 공통).
            addReserved( list, shaderslot::resname::kVisibleInstances, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kVisibleInstanceBuffer ), slotAt( 0, shaderslot::kVisibleInstanceBuffer ),
                         slotAt( vk::kTShift, shaderslot::kVisibleInstanceBuffer ), slotAt( 0, shaderslot::kVisibleInstanceBuffer ) );
            // GPU 가 변형한 정점 풀 t7 · 씬 라이트 목록 t8 (네 백엔드 공통).
            addReserved( list, shaderslot::resname::kMorphVertices, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kMorphVertexBuffer ), slotAt( 0, shaderslot::kMorphVertexBuffer ), slotAt( vk::kTShift, shaderslot::kMorphVertexBuffer ), slotAt( 0, shaderslot::kMorphVertexBuffer ) );
            addReserved( list, shaderslot::resname::kLights, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kLightBuffer ), slotAt( 0, shaderslot::kLightBuffer ), slotAt( vk::kTShift, shaderslot::kLightBuffer ), slotAt( 0, shaderslot::kLightBuffer ) );
            // 씬 배치 표: 그래픽스 t9 (네 백엔드 공통). 정점 셰이더가 자기 배치 번호로 읽는다.
            addReserved( list, shaderslot::resname::kBatches, ShaderBindingKind::StructuredBuffer,
                         slotAt( 0, shaderslot::kBatchBuffer ), slotAt( 0, shaderslot::kBatchBuffer ), slotAt( vk::kTShift, shaderslot::kBatchBuffer ), slotAt( 0, shaderslot::kBatchBuffer ) );
            // 정점 애니메이션(VAT) 표: 그래픽스 t10 (네 백엔드 공통). 먼 군중의 정점 셰이더가 인스턴스 시각으로 읽는다.
            addReserved( list, shaderslot::resname::kVertexAnimation, ShaderBindingKind::StructuredBuffer, slotAt( 0, shaderslot::kVertexAnimationBuffer ),
                         slotAt( 0, shaderslot::kVertexAnimationBuffer ), slotAt( vk::kTShift, shaderslot::kVertexAnimationBuffer ), slotAt( 0, shaderslot::kVertexAnimationBuffer ) );
            // 패스 전용 버퍼 t11 (네 백엔드 공통): 캔버스 사각형(canvas.hlsl) · 입자(particle.hlsl). 정점 · 픽셀 셰이더가 (루트 상수의 시작 + SV_InstanceID) 번째를 읽는다.
            addReserved( list, shaderslot::resname::kCanvasQuads, ShaderBindingKind::StructuredBuffer, slotAt( 0, shaderslot::kPassBuffer ), slotAt( 0, shaderslot::kPassBuffer ),
                         slotAt( vk::kTShift, shaderslot::kPassBuffer ), slotAt( 0, shaderslot::kPassBuffer ) );
            addReserved( list, shaderslot::resname::kParticles, ShaderBindingKind::StructuredBuffer, slotAt( 0, shaderslot::kPassBuffer ), slotAt( 0, shaderslot::kPassBuffer ),
                         slotAt( vk::kTShift, shaderslot::kPassBuffer ), slotAt( 0, shaderslot::kPassBuffer ) );
            // 라이트 그리드 t12 (네 백엔드 공통): 조명 식의 커서(lighting.hlsli LightGrid::loadWord)가 읽는다.
            addReserved( list, shaderslot::resname::kLightGrid, ShaderBindingKind::StructuredBuffer, slotAt( 0, shaderslot::kLightGridBuffer ), slotAt( 0, shaderslot::kLightGridBuffer ),
                         slotAt( vk::kTShift, shaderslot::kLightGridBuffer ), slotAt( 0, shaderslot::kLightGridBuffer ) );
            // 라이트 프로브 t13 (네 백엔드 공통): 조명 식이 프로브 볼륨 원소를 만나면 읽는다(lighting.hlsli LightProbe::load).
            addReserved( list, shaderslot::resname::kLightProbes, ShaderBindingKind::StructuredBuffer, slotAt( 0, shaderslot::kLightProbeBuffer ), slotAt( 0, shaderslot::kLightProbeBuffer ),
                         slotAt( vk::kTShift, shaderslot::kLightProbeBuffer ), slotAt( 0, shaderslot::kLightProbeBuffer ) );
            // lightgrid 컴퓨트: 라이트 t0 · 뷰 정보 t1 읽기, 그리드 u0 쓰기. GL 의 u# 은 SSBO SW_GL_UAV_BINDING0 + #.
            addReserved( list, shaderslot::resname::kLightGridLights, ShaderBindingKind::StructuredBuffer, slotAt( 0, 0 ), slotAt( 0, 0 ), slotAt( vk::kTShift, 0 ), slotAt( 0, 0 ) );
            addReserved( list, shaderslot::resname::kLightGridViews, ShaderBindingKind::StructuredBuffer, slotAt( 0, 1 ), slotAt( 0, 1 ), slotAt( vk::kTShift, 1 ), slotAt( 0, 1 ) );
            addReserved( list, shaderslot::resname::kLightGridRw, ShaderBindingKind::RwStructuredBuffer, slotAt( 0, 0 ), slotAt( 0, 0 ), slotAt( vk::kUShift, 0 ), slotAt( 0, shaderslot::gl::kUAVBinding0 ) );
            // gpucull 컴퓨트: t0/t1 읽기, u0/u1 쓰기. GL 의 u# 은 SSBO SW_GL_UAV_BINDING0 + #.
            addReserved( list, shaderslot::resname::kCullInstances, ShaderBindingKind::StructuredBuffer, slotAt( 0, 0 ), slotAt( 0, 0 ), slotAt( vk::kTShift, 0 ), slotAt( 0, 0 ) );
            addReserved( list, shaderslot::resname::kCullBatchInfo, ShaderBindingKind::StructuredBuffer, slotAt( 0, 1 ), slotAt( 0, 1 ), slotAt( vk::kTShift, 1 ), slotAt( 0, 1 ) );
            addReserved( list, shaderslot::resname::kCullIndirectArgs, ShaderBindingKind::RwStructuredBuffer, slotAt( 0, 0 ), slotAt( 0, 0 ), slotAt( vk::kUShift, 0 ), slotAt( 0, shaderslot::gl::kUAVBinding0 ) );
            addReserved( list, shaderslot::resname::kCullVisibleIDs, ShaderBindingKind::RwStructuredBuffer, slotAt( 0, 1 ), slotAt( 0, 1 ), slotAt( vk::kUShift, 1 ), slotAt( 0, shaderslot::gl::kUAVBinding0 + 1 ) );
            addReserved( list, shaderslot::resname::kCullLODState, ShaderBindingKind::RwStructuredBuffer, slotAt( 0, 2 ), slotAt( 0, 2 ), slotAt( vk::kUShift, 2 ), slotAt( 0, shaderslot::gl::kUAVBinding0 + 2 ) );
            // instanceanimation 컴퓨트: 인스턴스 버퍼를 u0 으로 고쳐 쓴다.
            addReserved( list, shaderslot::resname::kAnimationInstancesRw, ShaderBindingKind::RwStructuredBuffer, slotAt( 0, 0 ), slotAt( 0, 0 ), slotAt( vk::kUShift, 0 ), slotAt( 0, shaderslot::gl::kUAVBinding0 ) );
            // 엔진 텍스처 슬롯 t0..t3 / 머티리얼 텍스처 t16..t31: 에뮬 백엔드(DX11 · GL)만. Vulkan · DX12 는 선언 자체가 없어야 한다.
            static string s_arrEngineName[shaderslot::kEngineTextureCount];
            static string s_arrEngineSamplerName[shaderslot::kEngineTextureCount];
            static string s_arrMaterialName[shaderslot::kMaterialTextureCount];
            for ( uint32 slotIndex = 0; slotIndex < shaderslot::kEngineTextureCount; ++slotIndex )
            {
                s_arrEngineName[slotIndex]        = string( shaderslot::resname::kEngineTexture ) + to_string( slotIndex );
                s_arrEngineSamplerName[slotIndex] = s_arrEngineName[slotIndex] + "Sampler";
                const uint32 reg                  = shaderslot::kEngineTexture0 + slotIndex;
                addReserved( list, s_arrEngineName[slotIndex].c_str(), ShaderBindingKind::Texture, slotAt( 0, reg ), none(), none(), slotAt( 0, reg ) );
                addReserved( list, s_arrEngineSamplerName[slotIndex].c_str(), ShaderBindingKind::Sampler, slotAt( 0, reg ), none(), none(), slotAt( 0, reg ) );
            }
            // G버퍼 머티리얼 칸 t15: 에뮬 백엔드만. 짝 샘플러는 계약에 없다 — DX11 은 정적 세트로 읽고(s15), GL 은 결합 샘플러라 리플렉션에
            // 샘플러 이름이 따로 나오지 않는다(binding.hlsli). 어느 매니페스트에도 없는 이름을 계약에 두면 검사할 수 없다.
            addReserved( list, shaderslot::resname::kEngineGBufferMaterialTexture, ShaderBindingKind::Texture, slotAt( 0, shaderslot::kEngineGBufferMaterialTexture ), none(), none(),
                         slotAt( 0, shaderslot::kEngineGBufferMaterialTexture ) );
            for ( uint32 slotIndex = 0; slotIndex < shaderslot::kMaterialTextureCount; ++slotIndex )
            {
                s_arrMaterialName[slotIndex] = string( shaderslot::resname::kMaterialTexture ) + to_string( slotIndex );
                const uint32 reg             = shaderslot::kMaterialTexture0 + slotIndex;
                // 짝 샘플러 이름은 계약에 없다 — 셰이더가 머티리얼 칸을 짝 샘플러 없이 선언하고(binding.hlsli `SW_DECLARE_TEXTURE2D`),
                // DX11 은 정적 세트의 머티리얼 샘플러로, GL 은 유닛에 건 머티리얼 샘플러 객체로 읽는다(OpenGLRHIDeviceInit).
                addReserved( list, s_arrMaterialName[slotIndex].c_str(), ShaderBindingKind::Texture, slotAt( 0, reg ), none(), none(), slotAt( 0, reg ) );
            }
            // 컴퓨트 RW 텍스처 슬롯 u5..u7: 에뮬 백엔드만. GL 은 이미지 유닛(SSBO 와 다른 이름공간).
            static string s_arrRwTextureName[shaderslot::kComputeTextureUAVSlotCount];
            for ( uint32 slotIndex = 0; slotIndex < shaderslot::kComputeTextureUAVSlotCount; ++slotIndex )
            {
                s_arrRwTextureName[slotIndex] = string( shaderslot::resname::kRwTextureSlot ) + to_string( slotIndex );
                addReserved( list, s_arrRwTextureName[slotIndex].c_str(), ShaderBindingKind::RwTexture, slotAt( 0, shaderslot::kComputeTextureUAV0 + slotIndex ), none(), none(),
                             slotAt( 0, shaderslot::gl::kImageUnit0 + slotIndex ) );
            }
            // 네이티브 bindless 텍스처 배열: DX12 t0 space1 / Vulkan set 1 binding 0. RW 배열은 DX12 u0 space1 / Vulkan set 1 binding 3. 에뮬에는 없다.
            addReserved( list, shaderslot::resname::kBindlessTextures, ShaderBindingKind::Texture, none(),
                         at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkTextureBinding ), none() );
            addReserved( list, shaderslot::resname::kBindlessRwTextures, ShaderBindingKind::RwTexture, none(),
                         at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkRwTextureBinding ), none() );
            // 정적 샘플러 세트: DX12 s0..s6 배열 + s7 비교(루트 시그니처 정적 샘플러), Vulkan set 1 binding 1 배열 + binding 2 비교(immutable).
            // DX11 은 같은 세트를 s9..s15 샘플러 상태로 건다(bindStaticSamplers). GL 은 결합 샘플러뿐이라 없다.
            addReserved( list, shaderslot::resname::kSamplers, ShaderBindingKind::Sampler, none(),
                         none(), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkSamplerBinding ), none() );
            static string s_arrSamplerName[shaderslot::kStaticSamplerArrayCount];
            for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
            {
                s_arrSamplerName[samplerIndex] = string( shaderslot::resname::kSamplerSlot ) + to_string( samplerIndex );
                addReserved( list, s_arrSamplerName[samplerIndex].c_str(), ShaderBindingKind::Sampler, slotAt( 0, shaderslot::dx11::kStaticSampler0 + samplerIndex ),
                             at( 0, samplerIndex ), none(), none() );
            }
            addReserved( list, shaderslot::resname::kShadowSampler, ShaderBindingKind::Sampler, none(),
                         at( 0, shaderslot::kSamplerShadowCmp ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkShadowSamplerBinding ), none() );
            return list;
        }

        const vector<ShaderReservedBinding>& reservedBindings()
        {
            static const vector<ShaderReservedBinding> s_list = buildReservedBindings();
            return s_list;
        }
        /**
         * @brief 검사 규칙들입니다. 규칙마다 위반을 `report` 로 남기고 `_issueCount` 를 셉니다.
         * @details `ShaderBindingValidator::validate` 가 1) 예약 리소스 2 · 3) 리소스마다 백엔드 자리 규칙과 뒤 리소스와의 충돌 5) 정점 입력 순서로 부릅니다.
         *          2 · 3 은 리소스 하나씩 붙여 돌려 알림 순서가 예전(한 루프)과 같습니다.
         */
        struct ShaderBindingRuleChecks
        {
            using Internal = ShaderBindingValidatorInternal;

            vector<ShaderBindingValidatorIssue>* _pOutIssue{ nullptr };
            string_view                          _shaderLabel;
            ShaderTargetFormat                   _targetFormat{};
            bool                                 _bVulkan{ false };
            bool                                 _bOpenGl{ false };
            bool                                 _bDx12{ false };
            uint32                               _issueCount{ 0 };

            void report( const string& resource, string&& message ) { Internal::report( _pOutIssue, _shaderLabel, resource, std::move( message ), _issueCount ); }

            /** @brief 1) 예약 리소스: 종류와 위치가 이 백엔드 계약과 같은가. */
            void checkReservedBindings( const vector<Internal::ReflectedBinding>& listReflected )
            {
                const utf8* pLocationFormat = _bVulkan ? "set/binding" : "space/register";
                for ( const Internal::ReflectedBinding& reflected : listReflected )
                {
                    const ShaderReservedBinding* pReserved{ nullptr };
                    for ( const ShaderReservedBinding& reserved : reservedBindings() )
                    {
                        if ( reflected._name == reserved._name )
                        {
                            pReserved = &reserved;
                            break;
                        }
                    }
                    if ( pReserved == nullptr )
                        continue;

                    if ( Internal::kindMatches( pReserved->_kind, reflected._kind, _targetFormat ) == false )
                        report( reflected._name, string( "종류가 계약과 다릅니다 — 기대 " ) + Internal::kindName( pReserved->_kind ) + ", 리플렉션 " + Internal::kindName( reflected._kind ) );

                    const ShaderReservedLocation& expected = _bVulkan ? pReserved->_vulkan : ( _bOpenGl ? pReserved->_opengl : ( _bDx12 ? pReserved->_dx12 : pReserved->_dx11 ) );
                    if ( expected._bDeclared == false )
                    {
                        report( reflected._name, string( "이 백엔드 계약에는 없는 예약 리소스가 선언돼 있습니다 (리플렉션 " ) +
                                                     Internal::formatLocation( pLocationFormat, reflected._space, reflected._bindPoint ) + ")" );
                        continue;
                    }
                    if ( reflected._bindPoint != expected._bind || reflected._space != expected._space )
                    {
                        report( reflected._name, string( "위치가 계약과 다릅니다 — 기대 " ) + Internal::formatLocation( pLocationFormat, expected._space, expected._bind ) +
                                                     ", 리플렉션 " + Internal::formatLocation( pLocationFormat, reflected._space, reflected._bindPoint ) );
                    }
                }
            }

            /** @brief DX12 space0 슬롯 하나가 루트 시그니처의 슬롯 수 안인지 봅니다. */
            void checkDx12SlotLimit( const Internal::ReflectedBinding& reflected )
            {
                const Internal::RegisterClass registerClass = Internal::registerClassOf( reflected._kind );
                uint32                        limit         = 0;
                switch ( registerClass )
                {
                    case Internal::RegisterClass::ConstantBuffer:
                    {
                        limit = shaderslot::kConstantBufferSlotCount;
                        break;
                    }
                    case Internal::RegisterClass::ShaderResource:
                    {
                        limit = shaderslot::kSRVSlotCount;
                        break;
                    }
                    case Internal::RegisterClass::UnorderedAccess:
                    {
                        limit = shaderslot::kComputeUAVSlotCount;
                        break;
                    }
                    case Internal::RegisterClass::Sampler:
                    {
                        limit = shaderslot::kStaticSamplerCount;
                        break;
                    }
                    case Internal::RegisterClass::Other:
                    {
                        break;
                    }
                }
                if ( limit > 0 && reflected._bindPoint >= limit )
                    report( reflected._name, string( Internal::registerClassLetter( registerClass ) ) + to_string( reflected._bindPoint ) + " 은 루트 시그니처의 슬롯 수(" + to_string( limit ) + ")를 넘습니다" );
            }

            /** @brief 3) DX12: space0 = 슬롯(CB 는 루트 CBV, t · u 는 디스크립터 테이블, 그리고 정적 샘플러), space1 = 텍스처 배열(t0, 무제한), space2 = 루트 상수(b0). 그 밖은 루트 시그니처에 없다. */
            void checkDx12Space( const Internal::ReflectedBinding& reflected )
            {
                namespace bindless = shaderslot::bindless;
                if ( reflected._space == 0 )
                {
                    checkDx12SlotLimit( reflected );
                }
                else if ( reflected._space == bindless::kTextureSpace )
                {
                    const bool bKindOk                     = ( reflected._kind == ShaderBindingKind::Texture || reflected._kind == ShaderBindingKind::RwTexture );
                    const bool bViolatesUnboundedArrayRule = bKindOk == false || reflected._bindPoint != 0 || ( reflected._bindCount != Internal::ReflectedBinding::kUnknownCount && reflected._bindCount != 0 );
                    if ( bViolatesUnboundedArrayRule )
                        report( reflected._name, "space1 은 무제한 텍스처 배열(t0 / u0, []) 전용입니다" );
                }
                else if ( reflected._space == shaderslot::kRootConstantSpace )
                {
                    if ( reflected._kind != ShaderBindingKind::ConstantBuffer || reflected._bindPoint != shaderslot::kRootConstantRegister )
                        report( reflected._name, "space2 는 루트 상수(b0) 전용입니다" );
                }
                else
                {
                    report( reflected._name, string( "루트 시그니처에 없는 register space " ) + to_string( reflected._space ) + " 을 참조합니다" );
                }
            }

            /** @brief 3) Vulkan: 세트 0 은 종류별 시프트 범위, 세트 1 은 텍스처 배열 · 샘플러 · RW 텍스처 배열 전용이다. 그 밖의 세트는 파이프라인 레이아웃에 없다. */
            void checkVulkanSet( const Internal::ReflectedBinding& reflected )
            {
                namespace bindless = shaderslot::bindless;
                namespace vk       = shaderslot::vk;
                if ( reflected._space == 0 )
                {
                    const Internal::RegisterClass rangeClass = Internal::vulkanRangeClassOf( reflected._bindPoint );
                    if ( rangeClass == Internal::RegisterClass::Other )
                    {
                        report( reflected._name, string( "세트 0 의 슬롯 범위 밖 binding " ) + to_string( reflected._bindPoint ) + " 입니다 (b 0.., t " + to_string( vk::kTShift ) + ".., u " +
                                                     to_string( vk::kUShift ) + "..)" );
                    }
                    else if ( Internal::vulkanRangeAcceptsKind( rangeClass, reflected._kind ) == false )
                    {
                        report( reflected._name, string( "binding " ) + to_string( reflected._bindPoint ) + " 은 " + Internal::registerClassLetter( rangeClass ) + " 범위인데 리소스 종류가 " +
                                                     Internal::kindName( reflected._kind ) + " 입니다" );
                    }
                }
                else if ( reflected._space == bindless::kVkTextureSet )
                {
                    const bool bArray                  = ( reflected._bindPoint == bindless::kVkTextureBinding ) && ( reflected._kind == ShaderBindingKind::Texture || reflected._kind == ShaderBindingKind::Sampler );
                    const bool bSampler                = ( reflected._bindPoint == bindless::kVkSamplerBinding || reflected._bindPoint == bindless::kVkShadowSamplerBinding ) && reflected._kind == ShaderBindingKind::Sampler;
                    const bool bRwArray                = ( reflected._bindPoint == bindless::kVkRwTextureBinding ) && reflected._kind == ShaderBindingKind::RwTexture;
                    const bool bUnknownBindlessBinding = bArray == false && bSampler == false && bRwArray == false;
                    if ( bUnknownBindlessBinding )
                        report( reflected._name, "세트 1 은 텍스처 배열(binding 0)·샘플러(binding 1·2)·RW 텍스처 배열(binding 3) 전용입니다" );
                }
                else
                {
                    report( reflected._name, string( "파이프라인 레이아웃에 없는 descriptor set " ) + to_string( reflected._space ) + " 을 참조합니다 (세트는 0·1)" );
                }
            }

            /** @brief 2) 리소스 @p indexA 가 뒤의 리소스와 같은 자리를 차지하는지 봅니다(백엔드마다 이름공간이 다르다). */
            void checkCollisionsAfter( const vector<Internal::ReflectedBinding>& listReflected, size_t indexA )
            {
                const Internal::ReflectedBinding& reflectedA = listReflected[indexA];
                for ( size_t indexB = indexA + 1; indexB < listReflected.size(); ++indexB )
                {
                    const Internal::ReflectedBinding& reflectedB = listReflected[indexB];
                    if ( reflectedA._bindPoint != reflectedB._bindPoint )
                        continue;

                    bool   bCollide{ false };
                    string where;
                    if ( _bVulkan )
                    {
                        bCollide = ( reflectedA._space == reflectedB._space );
                        where    = Internal::formatLocation( "set/binding", reflectedA._space, reflectedA._bindPoint );
                    }
                    else if ( _bOpenGl )
                    {
                        const Internal::GlNamespace nsA = Internal::glNamespaceOf( reflectedA._kind );
                        bCollide                        = ( nsA != Internal::GlNamespace::Other && nsA == Internal::glNamespaceOf( reflectedB._kind ) );
                        where                           = string( Internal::glNamespaceName( nsA ) ) + " binding " + to_string( reflectedA._bindPoint );
                    }
                    else
                    {
                        const Internal::RegisterClass registerClassA = Internal::registerClassOf( reflectedA._kind );
                        bCollide                                     = ( reflectedA._space == reflectedB._space && registerClassA != Internal::RegisterClass::Other && registerClassA == Internal::registerClassOf( reflectedB._kind ) );
                        where                                        = string( Internal::registerClassLetter( registerClassA ) ) + to_string( reflectedA._bindPoint ) + " space" + to_string( reflectedA._space );
                    }
                    if ( bCollide )
                        report( reflectedA._name, string( "'" ) + reflectedB._name + "' 와 같은 자리를 차지합니다 (" + where + ")" );
                }
            }

            /** @brief 2 · 3) 리소스마다 백엔드 자리 규칙(DX12 space / Vulkan set · 범위 / GL set 0)을 보고 뒤 리소스와의 충돌을 봅니다. */
            void checkSlotRulesAndCollisions( const vector<Internal::ReflectedBinding>& listReflected )
            {
                for ( size_t indexA = 0; indexA < listReflected.size(); ++indexA )
                {
                    const Internal::ReflectedBinding& reflectedA = listReflected[indexA];
                    if ( _bDx12 && reflectedA._kind != ShaderBindingKind::Unknown )
                        checkDx12Space( reflectedA );
                    if ( _bVulkan && reflectedA._kind != ShaderBindingKind::Unknown )
                        checkVulkanSet( reflectedA );
                    if ( _bOpenGl && reflectedA._space != 0 )
                    {
                        report( reflectedA._name, string( "OpenGL 은 descriptor set 을 무시하는데 set " ) + to_string( reflectedA._space ) + " 로 선언돼 있습니다 — binding " +
                                                      to_string( reflectedA._bindPoint ) + " 하나로 취급됩니다" );
                    }
                    checkAssertSlot( reflectedA );
                    checkCollisionsAfter( listReflected, indexA );
                }
            }

            /** @brief 컴퓨트 쓰기 슬롯 u4 는 셰이더 assert 링(g_GPUAssertRW) 전용이고, 그 링은 u4 에만 놓인다. */
            void checkAssertSlot( const Internal::ReflectedBinding& reflected )
            {
                if ( reflected._space != 0 || Internal::registerClassOf( reflected._kind ) != Internal::RegisterClass::UnorderedAccess )
                    return;
                const uint32 bindOffset  = _bVulkan ? shaderslot::vk::kUShift : ( _bOpenGl ? shaderslot::gl::kUAVBinding0 : 0u );
                const bool   bAssertName = reflected._name == shaderslot::resname::kGPUAssertRing;
                const bool   bAssertSlot = reflected._bindPoint == bindOffset + shaderslot::kComputeAssertUAV;
                if ( bAssertName != bAssertSlot )
                    report( reflected._name, "컴퓨트 쓰기 슬롯 u4 는 셰이더 assert 링(g_GPUAssertRW) 전용이고 그 링은 u4 에만 놓입니다" );
            }

            /**
             * @brief 5) 정점 입력: 시맨틱이 정점 레이아웃 표(constant::arrVertexAttribute)에 있고, Vulkan · GL 은 location 까지 같은가.
             * @details DX 는 시맨틱 이름으로 묶어 순서가 달라도 맞지만, 두 백엔드는 **선언 순서**가 location 이라 중간 속성을
             *          빼먹으면 그 뒤가 한 칸씩 당겨진다(색을 읽으려다 노멀을 읽는다). 픽셀로는 늦게 드러난다.
             */
            void checkVertexInputs( const ShaderReflectionData& reflection )
            {
                for ( const ShaderVertexInputInfo& input : reflection._listVertexInput )
                {
                    const RHIVertexAttribute* pAttribute{ nullptr };
                    for ( const RHIVertexAttribute& attribute : constant::arrVertexAttribute )
                    {
                        if ( input._semantic == attribute._pSemanticName )
                        {
                            pAttribute = &attribute;
                            break;
                        }
                    }
                    const string label = input._semantic + to_string( input._semanticIndex );
                    if ( pAttribute == nullptr || input._semanticIndex != 0 )
                    {
                        report( label, "정점 레이아웃 표(constant::arrVertexAttribute)에 없는 정점 입력입니다 — 정점을 받는 셰이더는 VertexInput 을 쓸 것" );
                        continue;
                    }
                    if ( ( _bVulkan || _bOpenGl ) && input._location != pAttribute->_location )
                    {
                        report( label, string( "location 이 정점 레이아웃 표와 다릅니다 — 기대 " ) + to_string( pAttribute->_location ) + ", 리플렉션 " +
                                           to_string( input._location ) + " (Vulkan·GL 은 선언 순서로 location 을 매긴다 — VertexInput 을 쓸 것)" );
                    }
                }
            }
        };
    } // namespace

    const vector<ShaderReservedBinding>& ShaderBindingValidator::getReservedBindings()
    {
        return reservedBindings();
    }

    uint32 ShaderBindingValidator::validate( const ShaderReflectionData& reflection, ShaderTargetFormat targetFormat, string_view shaderLabel,
                                             vector<ShaderBindingValidatorIssue>* pOutIssue )
    {
        vector<ShaderBindingValidatorInternal::ReflectedBinding> listReflected;
        ShaderBindingValidatorInternal::collect( reflection, listReflected );

        ShaderBindingRuleChecks checks{};
        checks._pOutIssue    = pOutIssue;
        checks._shaderLabel  = shaderLabel;
        checks._targetFormat = targetFormat;
        checks._bVulkan      = ( targetFormat == ShaderTargetFormat::SPIRV_Vulkan );
        checks._bOpenGl      = ( targetFormat == ShaderTargetFormat::SPIRV_OpenGL );
        checks._bDx12        = ( targetFormat == ShaderTargetFormat::DXIL_D3D12 );
        checks.checkReservedBindings( listReflected );
        checks.checkSlotRulesAndCollisions( listReflected );
        checks.checkVertexInputs( reflection );
        return checks._issueCount;
    }
} // namespace sw
