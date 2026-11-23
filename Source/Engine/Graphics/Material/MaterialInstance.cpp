#include "pch.h"

#include "Engine/Graphics/Material/MaterialInstance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    namespace
    {
        struct MaterialInstanceInternal
        {
            /// @brief 리플렉션의 상수버퍼 변수 · 구조화 요소 변수 · 리소스 바인딩 중에 이름이 같은 것이 있는지 봅니다.
            static bool reflectionHasParam( const ShaderReflectionData& reflectionData, hashed_string paramName )
            {
                for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffer )
                {
                    for ( const ShaderVariableInfo& var : cb._listVariable )
                    {
                        if ( hashed_string( var._name.c_str() ) == paramName )
                            return true;
                    }
                }
                for ( const ShaderBufferInfo& element : reflectionData._listStructuredElement )
                {
                    for ( const ShaderVariableInfo& var : element._listVariable )
                    {
                        if ( hashed_string( var._name.c_str() ) == paramName )
                            return true;
                    }
                }
                for ( const ShaderReflectedBinding& resourceBinding : reflectionData._listResource )
                {
                    if ( hashed_string( resourceBinding._name.c_str() ) == paramName )
                        return true;
                }
                return false;
            }

            /**
             * @brief 부모의 매개변수 모양(셰이더 타입)이 쓰려는 값의 모양과 맞는지 봅니다. 어긋나면 로그 id 와 함께 false 입니다.
             * @details float4 글("0.5 0 0 0")을 스칼라(Float/Range)에 쓰면 패킹이 못 읽는 글로 거절한다. 부모에 없는 이름은 다른 변형이 쓸 수 있어 통과시킨다.
             *          쓸 때가 아니라 렌더 사본을 만들 때(`rebuildRenderData`, 게임 스레드, 부모 초기화 뒤) 본다 — 부모의 속성 목록은 디바이스 초기화가 채우고
             *          맞추므로 쓸 때 읽으면 비어 있거나(검사가 조용히 빠진다) 비동기 씬 로드 워커에서 게임 스레드의 초기화와 겹친다(언리얼 MID 도 쓸 때는 이름 · 값만 둔다).
             */
            [[nodiscard]] static bool canAcceptShape( const Material* pParent, hashed_string name, bool bScalar )
            {
                const MaterialProperty* pProp = pParent != nullptr ? pParent->findProperty( name ) : nullptr;
                if ( pProp == nullptr )
                    return true;
                const MaterialPropertyKind shaderType = pProp->_shaderType;
                const bool                 bIsScalar  = shaderType == MaterialPropertyKind::Float;
                const bool                 bIsVector  = shaderType == MaterialPropertyKind::Float2 || shaderType == MaterialPropertyKind::Float3 || shaderType == MaterialPropertyKind::Float4;
                if ( ( bScalar && bIsVector ) || ( bScalar == false && bIsScalar ) )
                {
                    SW_LOG_ERROR_ID( "Material.ParameterShapeMismatch", "Material parameter '%#' is %# and cannot take a %# value", pProp->_name, MaterialPacking::typeToString( shaderType ),
                                     bScalar ? "scalar" : "vector" );
                    return false;
                }
                return true;
            }

            /** @brief 값을 넣거나 바꿉니다. 이미 같은 값이면 false 입니다(매 프레임 같은 키워드를 켜는 코드가 퍼뮤테이션 변경을 적지 않게). */
            static bool insertOrAssign( vector<pair<hashed_string, bool>>& listPair, hashed_string key, bool val )
            {
                for ( pair<hashed_string, bool>& entry : listPair )
                {
                    if ( entry.first == key )
                    {
                        if ( entry.second == val )
                            return false;
                        entry.second = val;
                        return true;
                    }
                }
                listPair.push_back( { key, val } );
                return true;
            }

            /** @brief 부모가 선언한 정적 스위치를 이름이나 켬 키워드로 찾습니다(`Material::setStaticSwitch` 와 같은 규칙). 부모가 없거나 없으면 nullptr 입니다. */
            static const MaterialStaticSwitch* findSwitch( const Material* pParent, hashed_string name )
            {
                if ( pParent == nullptr )
                    return nullptr;
                for ( const MaterialStaticSwitch& entry : pParent->getPermutations()._listStaticSwitch )
                {
                    if ( hashed_string( entry._name.c_str() ) == name || hashed_string( entry._keyword.c_str() ) == name )
                        return &entry;
                }
                return nullptr;
            }

            /** @brief 부모의 @p name 칸이 키워드를 내는 Bool · Keyword 칸(`shaderKeyword`)이면 true 입니다 — 그 값 덮어쓰기는 define 을 바꾼다(`Material::computeShaderDefines`). */
            static bool isKeywordValueProperty( const Material* pParent, hashed_string name )
            {
                const MaterialProperty* pProp = pParent != nullptr ? pParent->findProperty( name ) : nullptr;
                return pProp != nullptr && ( pProp->_type == MaterialPropertyKind::Bool || pProp->_type == MaterialPropertyKind::Keyword ) && pProp->_shaderKeyword.empty() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    shared_ptr<MaterialInstance> MaterialInstance::create( Material* pParentMaterial )
    {
        // 인자가 Material* 이라 ADL 이 std::make_shared 를 끌어온다. 한정해야 sw 것이 잡힌다.
        return sw::make_shared<MaterialInstance>( CreateKey{}, pParentMaterial );
    }

    MaterialInstance::MaterialInstance( CreateKey, Material* pParentMaterial )
        : _pParentMaterial{ pParentMaterial }
        , _listValueOverride{}
        , _listTextureOverride{}
        , _listKeywordOverride{}
        , _renderProxy{ MaterialRenderProxy::create() }
        , _renderData{}
        , _parentBufferGeneration{ 0 }
        , _overrideRevision{}
        , _renderDataOverrideRevision{ 0 }
        , _pTextureDevice{ nullptr }
        , _listCachedDefine{}
        , _cachedPermutationHash{ 0 }
        , _previousPermutationHash{ 0 }
        , _parentPermutationHash{ 0 }
        , _parentLoadGeneration{ 0 }
        , _bDefinesDirty{ SW_TRUE }
        , _instReserved{ 0 }
        , _renderResourceHook{ make_unique<RHIRenderResourceHook<MaterialInstance>>( *this ) }
    {
    }

    MaterialInstance::~MaterialInstance()
    {
        // 텍스처를 요청한 디바이스가 죽었으면 clearRHI 가 이미 비웠다 — 남아 있으면 살아 있는 디바이스다. 상수 버퍼는 프록시가 놓는다.
        releaseTextureOverrides();
        MaterialCache* const pCache = MaterialCache::findBound();
        if ( pCache != nullptr )
            pCache->getCompletionWatch().unwatch( this ); // 로드를 기다리던 중이면 지켜보기에서 뺀다(폴링과 같은 잠금 — 묻는 중에 사라지지 않는다)
    }

    MaterialCompletionWatch::PollResult MaterialInstance::pollTextureCompletion( void* pOwner )
    {
        return static_cast<const MaterialInstance*>( pOwner )->hasPendingTextureLoad() ? MaterialCompletionWatch::PollResult::Pending : MaterialCompletionWatch::PollResult::Complete;
    }

    bool MaterialInstance::hasPendingTextureLoad() const
    {
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._load.isValid() && texture._load.isReady() == false && texture._load.hasFailed() == false )
                return true;
        }
        return false;
    }

    void MaterialInstance::clearRHI( IRHIDevice* pDevice )
    {
        // 텍스처는 디바이스와 함께 갔다(TextureCache 의 Texture2D 도 같은 통보를 받는다). 참조만 놓는다.
        if ( _pTextureDevice == pDevice )
            releaseTextureOverrides();
    }

    void MaterialInstance::releaseRHI( IRHIDevice* pRHI )
    {
        if ( pRHI == nullptr || _pTextureDevice == pRHI )
            releaseTextureOverrides();
    }

    MaterialRenderBinding MaterialInstance::getRenderBinding( const MaterialRenderInputs& inputs )
    {
        if ( _pParentMaterial == nullptr )
            return MaterialRenderBinding{};
        // 부모를 직접 그리지 않아도 부모의 레이아웃 · 텍스처 SRV 를 맞춘다 — 그래야 아래 세대 비교가 부모의 새 바이트를 본다.
        _pParentMaterial->syncRenderState( inputs );
        // 텍스처는 부모가 빌린 디바이스로 요청한다. 부모가 아직 초기화 전이면 디바이스가 없어 덮어쓴 텍스처를 미룬다(그동안 부모 텍스처가 보인다).
        IRHIDevice* pDevice = _pParentMaterial->getRHIDevice();
        // 텍스처 덮어쓰기의 새 SRV(다시 올림 · 비동기 로드 끝)는 텍스처 캐시의 세대가 알린다 — 장면 스냅샷도 그 세대를 함께 본다.
        const bool bTextureChanged = pDevice != nullptr && syncTextureOverrides( pDevice );
        // 부모 값이 바뀌었으면(값 · 레이아웃 · 다시 로드 · 텍스처 슬롯) 사본도 낡았다. 덮어쓰기가 바뀔 때만 다시 만들면 덮어쓰지 않은 파라미터에서
        // 부모의 값 변경과 다시 맞춘 레이아웃을 놓친다.
        const bool bParentChanged   = _parentBufferGeneration != _pParentMaterial->getBufferGeneration();
        const bool bOverrideChanged = _overrideRevision.get() != _renderDataOverrideRevision;
        if ( bTextureChanged || bParentChanged || bOverrideChanged || _renderData == nullptr )
            rebuildRenderData( pDevice );
        return MaterialRenderBinding{ _renderProxy, _renderData };
    }

    void MaterialInstance::rebuildRenderData( IRHIDevice* pDevice )
    {
        shared_ptr<MaterialRenderData> data = sw::make_shared<MaterialRenderData>();
        data->_bytes                        = _pParentMaterial->getBuffer();
        data->_elementStride                = _pParentMaterial->getElementStride();
        _parentBufferGeneration             = _pParentMaterial->getBufferGeneration();
        _renderDataOverrideRevision         = _overrideRevision.get();
        // 부모 바이트가 비었으면(레이아웃 오류 — 오류는 부모가 남겼다) 빈 사본이다. 렌더 스레드는 빈 바이트를 올리지 않는다.
        if ( data->_bytes.empty() == false )
        {
            dropShapeMismatchedOverrides();
            for ( const ValueOverride& valueOverride : _listValueOverride )
            {
                if ( valueOverride._kind == ValueKind::Text )
                    _pParentMaterial->packNamedValueIntoBuffer( valueOverride._name, valueOverride._text, data->_bytes );
                else
                    _pParentMaterial->packNamedFloatsIntoBuffer( valueOverride._name, valueOverride._arrValue, valueOverride.getFloatCount(), data->_bytes );
            }
            // 덮어쓴 텍스처: 네이티브 bindless 는 지금 SRV 인덱스를, 슬롯 바인딩 백엔드(DX11 · GL)는 슬롯 서수를 넣는다(부모와 같은 규칙).
            const bool bNativeBindless = pDevice != nullptr && pDevice->supportsNativeBindlessSampling();
            for ( const TextureOverride& texture : _listTextureOverride )
            {
                const RHIDescriptorIndex srv = texture.getStableSRV();
                if ( srv == kInvalidDescriptorIndex )
                    continue;
                const optional<uint32> textureBinding = bNativeBindless ? optional<uint32>{ srv } : findTextureSlot( texture._name );
                if ( textureBinding.has_value() && *textureBinding != kInvalidDescriptorIndex )
                    _pParentMaterial->packTextureIntoBuffer( texture._name, *textureBinding, data->_bytes );
            }
        }
        fillTextureSlots( data->_arrTextureSRV, shaderslot::kMaterialTextureCount );
        _renderData = std::move( data );
    }

    void MaterialInstance::dropShapeMismatchedOverrides()
    {
        // 어긋난 값은 한 번 알리고 버린다(쓸 때 거절하던 것과 같은 결과) — 남겨 두면 사본을 만들 때마다 다시 알린다.
        const Material* pParent = _pParentMaterial;
        VectorUtil::removeIf( _listValueOverride, [pParent]( const ValueOverride& valueOverride )
        { return valueOverride._kind != ValueKind::Text && MaterialInstanceInternal::canAcceptShape( pParent, valueOverride._name, valueOverride._kind == ValueKind::Scalar ) == false; } );
    }

    string MaterialInstance::ValueOverride::makeText() const
    {
        return _kind == ValueKind::Text ? _text : MaterialPacking::formatFloats( _arrValue, getFloatCount() );
    }

    MaterialInstance::ValueOverride* MaterialInstance::findValueOverride( hashed_string name )
    {
        for ( ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
                return &valueOverride;
        }
        return nullptr;
    }

    void MaterialInstance::assignFloats( hashed_string name, ValueKind kind, const float32* pValue, uint32 count )
    {
        if ( rejectStaticValue( name ) )
            return;
        ValueOverride* pOverride = findValueOverride( name );
        if ( pOverride == nullptr )
        {
            pOverride        = &_listValueOverride.emplace_back();
            pOverride->_name = name;
        }
        else if ( pOverride->_kind == kind && Memory::compare( pOverride->_arrValue, pValue, count * sizeof( float32 ) ) == 0 )
        {
            return;
        }
        pOverride->_text.clear();
        pOverride->_kind = kind;
        Memory::copy( pOverride->_arrValue, pValue, count * sizeof( float32 ) );
        markRenderDirty();
    }

    bool MaterialInstance::syncTextureOverrides( IRHIDevice* pRHI )
    {
        if ( _listTextureOverride.empty() || engine::areEngineServicesBound() == false )
            return false;
        TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        bool          bChanged{ false };
        // 디바이스가 바뀌었으면 옛 디바이스로 요청한 것을 놓고 새로 요청한다(옛 것이 이미 죽었으면 clearRHI 가 먼저 와서 비웠다).
        if ( _pTextureDevice != nullptr && _pTextureDevice != pRHI )
        {
            releaseTextureOverrides();
            bChanged = true;
        }
        _pTextureDevice = pRHI;

        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._acquiredPath != texture._assetPath )
            {
                texture._load.reset();
                texture._acquiredPath = texture._assetPath;
                bChanged              = true;
                // 게임 스레드가 디스크를 기다리지 않게 워커가 읽는다 — 다 읽기 전 프레임은 로드 중 텍스처가 보인다(`Material` 의 텍스처 자리와 같은 길, 언리얼 MID 도
                // 스트리밍 중인 텍스처 파라미터는 낮은 밉 · 기본 텍스처로 그린다). 요청이 곧바로 번호를 주고 업로드가 끝나면 같은 번호를 다시 묶으므로(R-6) 다시 패킹하지 않는다.
                if ( texture._assetPath.empty() == false )
                    texture._load = textures.request( texture._assetPath, AssetLoadStart::Async, TextureStreamingGroup::Streamable );
                // 끝나는 때(성공 · 실패)를 머티리얼 캐시가 지켜본다 — 끝나면 이 인스턴스가 렌더 값 변경 기록에 실려 다음 맞추기가 다시 본다(실패면 부모 텍스처로 다시 패킹).
                // 아무도 이 인스턴스를 만지지 않는 프레임에 실패가 오면, 바뀐 주인만 맞추는 장면 빌더가 그것을 보지 못한다.
                MaterialCache* const pCache = MaterialCache::findBound();
                if ( pCache != nullptr && hasPendingTextureLoad() )
                    pCache->getCompletionWatch().watch( this, &MaterialInstance::pollTextureCompletion );
            }
            if ( texture._load.hasFailed() )
            {
                SW_LOG_WARNING_ID( "MaterialInstance.MaterialInstanceTextureCouldNot", "Material instance of '%#': texture '%#' for '%#' could not be loaded (%#) — the parent's texture stays.", getParentName(),
                                   texture._assetPath.c_str(), texture._name.c_str(), texture._load.getError().c_str() );
                texture._load.reset(); // 경로는 적어 두었다 — 다시 묻지 않는다(같은 경로를 다시 주면 `setTextureParameter` 가 다시 시도하게 한다)
                bChanged = true;       // 쥐던 번호(누락 체커)를 놓았다 — 부모의 텍스처로 다시 패킹한다
            }
        }
        // 지운 덮어쓰기(빈 경로)는 놓은 뒤 뺀다.
        VectorUtil::removeIf( _listTextureOverride, []( const TextureOverride& texture )
        { return texture._assetPath.empty() && texture._load.isValid() == false; } );

        return bChanged;
    }

    void MaterialInstance::releaseTextureOverrides()
    {
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._load.reset(); // 참조는 핸들이 쥐었다 — 놓으면 캐시에 돌려준다(`Material::releaseTextureAssets` 와 같다)
            texture._acquiredPath.clear();
        }
        _pTextureDevice = nullptr;
        markRenderDirty();
    }

    optional<uint32> MaterialInstance::findTextureSlot( hashed_string name ) const
    {
        if ( _pParentMaterial == nullptr )
            return nullopt;
        // 서수는 부모의 텍스처 프로퍼티 차례로 고정이다(빈 칸 포함) — 덮어쓰기는 같은 칸에 든다. 셰이더 변형이 그 칸만 선언하므로 뒤에 칸을 잇지 않는다.
        const optional<uint32> parentSlot = _pParentMaterial->findTextureSlot( name );
        return parentSlot.has_value() && *parentSlot < shaderslot::kMaterialTextureCount ? parentSlot : nullopt;
    }

    void MaterialInstance::fillTextureSlots( RHIDescriptorIndex* pOutSlot, uint32 slotCount ) const
    {
        if ( pOutSlot == nullptr || _pParentMaterial == nullptr )
            return;
        const vector<RHIDescriptorIndex>& listParentSRV = _pParentMaterial->getMaterialTextureSRVs();
        for ( uint32 slot = 0; slot < slotCount; ++slot )
        {
            pOutSlot[slot] = slot < listParentSRV.size() ? listParentSRV[slot] : kInvalidDescriptorIndex;
        }
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            const RHIDescriptorIndex srv = texture.getStableSRV();
            if ( srv == kInvalidDescriptorIndex )
                continue;
            const optional<uint32> slot = findTextureSlot( texture._name );
            if ( slot.has_value() && *slot < slotCount )
                pOutSlot[*slot] = srv;
        }
    }

    void MaterialInstance::clearOverrides()
    {
        _listValueOverride.clear();
        // 텍스처는 돌려준 뒤 뺀다(syncTextureOverrides) — 여기서 지우면 빌린 참조를 잃는다.
        for ( TextureOverride& texture : _listTextureOverride )
        {
            texture._assetPath.clear();
        }
        _listKeywordOverride.clear();
        _bDefinesDirty = SW_TRUE;
        MaterialPermutation::recordPermutationChange( this );
        markRenderDirty();
    }

    bool MaterialInstance::enableKeyword( hashed_string keyword )
    {
        return setRuntimeSwitch( keyword, true );
    }

    bool MaterialInstance::disableKeyword( hashed_string keyword )
    {
        return setRuntimeSwitch( keyword, false );
    }

    bool MaterialInstance::setRuntimeSwitch( hashed_string keyword, bool bEnabled )
    {
        const MaterialStaticSwitch* pSwitch = MaterialInstanceInternal::findSwitch( _pParentMaterial, keyword );
        if ( pSwitch == nullptr )
        {
            SW_LOG_ERROR_ID( "MaterialInstance.KeywordUnknown", "Material instance: the parent material '%#' declares no static switch '%#'", getParentName(), keyword.c_str() );
            return false;
        }
        if ( pSwitch->_bShaderFeature != SW_FALSE )
        {
            SW_LOG_ERROR_ID( "MaterialInstance.KeywordNotRuntime", "Material instance of '%#': static switch '%#' is a shader feature (bShaderFeature=\"1\") - only asset states are cooked, so set it in a child material (_base)",
                             getParentName(), keyword.c_str() );
            return false;
        }
        // 덮어쓰기는 스위치 이름으로 든다 — 키워드로 켜도 이름으로 켜도 한 칸이다.
        if ( MaterialInstanceInternal::insertOrAssign( _listKeywordOverride, hashed_string( pSwitch->_name.c_str() ), bEnabled ) == false )
            return true; // 이미 그 상태다 — 퍼뮤테이션이 그대로다
        _bDefinesDirty = SW_TRUE;
        MaterialPermutation::recordPermutationChange( this );
        markRenderDirty();
        return true;
    }

    bool MaterialInstance::rejectStaticValue( hashed_string name ) const
    {
        if ( MaterialInstanceInternal::isKeywordValueProperty( _pParentMaterial, name ) == false )
            return false;
        SW_LOG_ERROR_ID( "MaterialInstance.StaticValueRejected",
                         "Material instance of '%#': '%#' emits a shader keyword - a runtime value would make an uncooked permutation, so set it in a child material (_base) or use a runtime switch (enableKeyword)",
                         getParentName(), name.c_str() );
        return true;
    }

    bool MaterialInstance::setParameter( hashed_string name, string_view value )
    {
        if ( rejectStaticValue( name ) )
            return false;
        ValueOverride* pOverride = findValueOverride( name );
        if ( pOverride == nullptr )
        {
            pOverride        = &_listValueOverride.emplace_back();
            pOverride->_name = name;
        }
        else if ( pOverride->_kind == ValueKind::Text && pOverride->_text == value )
        {
            return true;
        }
        pOverride->_kind = ValueKind::Text;
        pOverride->_text = value;
        markRenderDirty();
        return true;
    }

    void MaterialInstance::setScalarParameter( hashed_string name, float32 value )
    {
        assignFloats( name, ValueKind::Scalar, &value, 1 );
    }

    void MaterialInstance::setVectorParameter( hashed_string name, const float4& value )
    {
        const float32 arrValue[4] = { value._x, value._y, value._z, value._w };
        assignFloats( name, ValueKind::Vector, arrValue, 4 );
    }

    void MaterialInstance::setTextureParameter( hashed_string name, string_view textureAssetPath )
    {
        // 원하는 경로만 적는다. 빌리고 돌려주는 것은 다음 getRenderBinding 이다(syncTextureOverrides).
        markRenderDirty();
        for ( TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name != name )
                continue;
            texture._assetPath = string( textureAssetPath );
            // 같은 경로를 다시 주면 지난번에 읽지 못한 것을 다시 시도한다(파일을 고친 뒤). 읽는 중이거나 준비된 것은 그대로 둔다.
            if ( texture._load.isValid() == false )
                texture._acquiredPath.clear();
            return;
        }
        if ( textureAssetPath.empty() )
            return;
        TextureOverride texture{};
        texture._name      = name;
        texture._assetPath = string( textureAssetPath );
        _listTextureOverride.push_back( std::move( texture ) );
    }

    string_view MaterialInstance::getParentName() const
    {
        return _pParentMaterial != nullptr ? string_view( _pParentMaterial->getName() ) : string_view( "<none>" );
    }

    MaterialPermutationDef MaterialInstance::makePermutations() const
    {
        MaterialPermutationDef permutations = _pParentMaterial->getPermutations();
        for ( const pair<hashed_string, bool>& keywordOverride : _listKeywordOverride )
        {
            const hashed_string&  name    = keywordOverride.first;
            MaterialStaticSwitch* pSwitch = VectorUtil::findIf( permutations._listStaticSwitch, [&name]( const MaterialStaticSwitch& entry )
            { return hashed_string( entry._name.c_str() ) == name; } );
            if ( pSwitch == nullptr )
            {
                // 켠 뒤 부모를 다시 읽어 스위치가 없어졌다 — 부모가 선언한 칸만 define 이 되어, 쿠킹하지 않은 define 글이 퍼뮤테이션에 섞이지 않는다.
                SW_LOG_ERROR_ID( "MaterialInstance.KeywordUnknown", "Material instance: the parent material '%#' declares no static switch '%#'", getParentName(), name.c_str() );
                continue;
            }
            pSwitch->_bEnabled = keywordOverride.second ? 1 : 0;
        }
        return permutations;
    }

    bool MaterialInstance::getParameter( hashed_string name, string& outValue ) const
    {
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
            {
                outValue = valueOverride.makeText();
                return true;
            }
        }
        if ( _pParentMaterial != nullptr )
        {
            const MaterialProperty* prop = _pParentMaterial->findProperty( name );
            if ( prop != nullptr )
            {
                outValue = prop->_value.empty() == false ? prop->_value : prop->_defaultValue;
                return true;
            }
        }
        return false;
    }

    string MaterialInstance::getTextureParameter( hashed_string name ) const
    {
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name == name && texture._assetPath.empty() == false )
                return texture._assetPath;
        }
        if ( _pParentMaterial != nullptr )
        {
            const MaterialProperty* pProp = _pParentMaterial->findProperty( name );
            if ( pProp != nullptr )
                return pProp->_assetPath;
        }
        return string{};
    }

    const vector<string>& MaterialInstance::getCachedShaderDefines() const
    {
        refreshCachedDefines();
        return _listCachedDefine;
    }

    void MaterialInstance::refreshCachedDefines() const
    {
        // 부모 해시만 보면 부모를 다시 읽어 덮은 칸의 뜻(스위치 키워드 이름 · 키워드 칸 선언)만 바뀐 경우를 놓친다 — 읽기 세대도 함께 본다.
        const uint64 currentParentHash           = _pParentMaterial != nullptr ? _pParentMaterial->getPermutationHash() : 0;
        const uint32 currentParentLoadGeneration = _pParentMaterial != nullptr ? _pParentMaterial->getLoadGeneration() : 0;
        if ( _parentPermutationHash != currentParentHash || _parentLoadGeneration != currentParentLoadGeneration )
        {
            _parentPermutationHash = currentParentHash;
            _parentLoadGeneration  = currentParentLoadGeneration;
            _bDefinesDirty         = SW_TRUE;
        }

        if ( _bDefinesDirty == SW_TRUE )
        {
            _previousPermutationHash = _cachedPermutationHash; // 처음 만들 때는 0 이다
            if ( _pParentMaterial != nullptr )
            {
                // 부모와 같은 정의 함수 · 해시 규칙이다 — 렌더 상태 · 그리기 순서는 부모의 것이라 다른 렌더 상태의 부모가 한 배치로 합쳐지지 않는다.
                _pParentMaterial->computeShaderDefines( makePermutations(), _listCachedDefine );
                _cachedPermutationHash = _pParentMaterial->computePermutationHash( _listCachedDefine );
            }
            else
            {
                _listCachedDefine.clear();
                _cachedPermutationHash = MaterialPermutation::hashDefines( _listCachedDefine );
            }
            _bDefinesDirty = SW_FALSE;
        }
    }

    uint64 MaterialInstance::getPermutationHash() const
    {
        refreshCachedDefines();
        return _cachedPermutationHash;
    }

    uint64 MaterialInstance::getPreviousPermutationHash() const
    {
        refreshCachedDefines();
        return _previousPermutationHash;
    }

    bool MaterialInstance::isParameterOverridden( hashed_string name ) const
    {
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( valueOverride._name == name )
                return true;
        }
        for ( const TextureOverride& texture : _listTextureOverride )
        {
            if ( texture._name == name && texture._assetPath.empty() == false )
                return true;
        }
        return VectorUtil::containsIf( _listKeywordOverride, [&name]( const pair<hashed_string, bool>& entry )
        { return entry.first == name; } );
    }

    bool MaterialInstance::validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const
    {
        for ( const ValueOverride& valueOverride : _listValueOverride )
        {
            if ( MaterialInstanceInternal::reflectionHasParam( reflectionData, valueOverride._name ) == false )
                return false;
        }
        return true;
    }

} // namespace sw
