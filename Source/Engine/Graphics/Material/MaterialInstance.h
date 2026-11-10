/**
 * @file MaterialInstance.h
 * @brief 마스터 Material 위에 드로우나 액터 단위로 덮어쓰는 오버라이드를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    class Texture2D;

    /**
     * @class MaterialInstance
     * @brief 마스터 Material 위에 드로우나 액터 단위로 덮어쓰는 오버라이드입니다(UE 의 MaterialInstanceDynamic).
     * @note 값 · 텍스처 · 키워드만 덮는다. 렌더 상태 · `alphaMode` · `renderQueue` · 양면 조명은 부모의 것이다 — 드로우마다 PSO 를 가르면 인스턴스의 값이 머티리얼 원소 하나로 합쳐지는 이득을 잃는다
     *       (언리얼 Dynamic Instance · 유니티 MaterialPropertyBlock 도 상태를 바꾸지 못한다, 기억에 의존한 비교). 다른 렌더 상태가 필요하면 `parent=` 로 자식 머티리얼을 만든다.
     *       문턱값은 프로퍼티(`_alphaCutoff`)라 덮을 수 있다.
     * @details 게임 스레드 객체다(`Material` 과 같은 규칙). 덮어쓴 텍스처 요청과 바이트 패킹도 게임 스레드가 하고, 렌더 스레드는 `getRenderBinding` 의
     *          불변 사본과 프록시만 받는다. 예전에는 렌더 스레드의 `updateRHI` 가 이 객체를 잠금 아래에서 함께 고쳤다.
     */
    class SW_API MaterialInstance final : public RHIRenderResource
    {
    public:
        /** @brief 머티리얼 인스턴스 에셋 종류입니다(등록은 `MaterialInstance.cpp` 의 `SW_ASSET_TYPE`). 경로에 정해진 접미사가 없어 접미사를 두지 않는다. 판 0. */
        static constexpr AssetTypeDef kAssetType{ "MaterialInstance", nullptr, 0, {}, 0, {} };

        /**
         * @brief 생성 키입니다. create() 만 만들 수 있습니다.
         * @details 생성자가 이 키를 요구하므로 `make_shared<MaterialInstance>()` 도 스택의 `MaterialInstance x;` 도 **컴파일되지 않는다.**
         *          모든 MaterialInstance 가 Engine 안에서 shared_ptr 로 만들어진다는 것을 컴파일러가 보장한다. 그래서 렌더 패킷이
         *          소유권을 가질 수 있고, 제어 블록이 모듈 DLL 에 있는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class MaterialInstance;
        };
        /** @brief create() 전용 생성자입니다. 마스터 머티리얼에 연결됩니다. */
        MaterialInstance( CreateKey, Material* pParentMaterial );
        /**
         * @brief Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 제어 블록(소멸 코드)은 make_shared 를 부른 DLL 에 있다. 렌더 패킷(GPUScene 스냅샷)이 소유권을 함께
         *          싣고 있으므로 게임 모듈이 만든 인스턴스를 엔진이 마지막까지 가질 수 있고, 모듈이 언로드된 뒤 놓으면
         *          없는 코드로 뛰어든다. 여기서 만들면 누가 마지막에 놓든 Engine 코드가 실행된다.
         */
        static shared_ptr<MaterialInstance> create( Material* pParentMaterial );
        /** @brief 덮어쓴 텍스처를 놓습니다. */
        ~MaterialInstance() override;

        /** @brief 복사를 금지합니다. */
        MaterialInstance( const MaterialInstance& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialInstance& operator=( const MaterialInstance& ) = delete;

        /** @brief (RHIRenderResource) 그 디바이스로 요청한 덮어쓴 텍스처를 놓습니다. 상수 버퍼는 프록시가 같은 통보를 받는다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 텍스처 참조만 놓습니다. */
        void clearRHI( IRHIDevice* pDevice ) override;

        /** @brief 오버라이드만 있는 MaterialInstanceDef XML 을 로드합니다. 부모는 따로 설정합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 인스턴스 XML 을 저장합니다. */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /**
         * @brief 렌더 스레드에 건널 (프록시, 부모 바이트에 덮어쓰기를 더한 불변 사본)입니다(게임 스레드). 덮어쓰기 · 부모 값 · 텍스처가 바뀐 뒤 처음 부를 때만 사본을 새로 만든다.
         * @details 덮어쓴 텍스처의 요청 · 반환도 여기서 맞춘다(`syncTextureOverrides`). 부모가 없으면 빈 쌍이고, 부모 바이트가 비면(레이아웃 오류) 사본의 바이트도 비어 렌더 스레드가 건너뛴다.
         */
        MaterialRenderBinding getRenderBinding();
        /** @brief 오버라이드를 모두 지웁니다. */
        void clearOverrides();
        /** @brief 키워드를 켭니다. */
        void enableKeyword( hashed_string keyword );
        /** @brief 키워드를 끕니다. */
        void disableKeyword( hashed_string keyword );

        /** @brief 부모 머티리얼을 설정합니다. */
        void setParent( Material* pParentMaterial );
        /** @brief 인스턴스 이름을 설정합니다. */
        void setName( string_view name ) { _desc._name = name; }
        /** @brief 일반 오버라이드입니다(Enum/BitFlag/Color/Range/Bool/스칼라 텍스트). */
        void setParameter( hashed_string name, string_view value );
        /** @brief 스칼라 파라미터를 설정합니다. */
        void setScalarParameter( hashed_string name, float32 value );
        /** @brief 벡터 파라미터를 설정합니다. */
        void setVectorParameter( hashed_string name, const float4& value );
        /**
         * @brief 텍스처 파라미터를 **텍스처 에셋**으로 덮어씁니다(리소스 경로, 예: `engine/textures/perlin.dds`). 빈 경로면 덮어쓰기를 지웁니다.
         * @details 언리얼 MIC 의 `SetTextureParameterValue( UTexture* )`, 유니티 `MaterialPropertyBlock.SetTexture( Texture )` 처럼 디스크립터
         *          인덱스가 아니라 텍스처 자체를 가리킨다. 경로만 적고, 다음 `getRenderBinding` 이 TextureCache 로 요청하고 반환한다.
         *          셰이더에 넣는 값(네이티브 bindless 의 SRV 인덱스, DX11 과 GL 의 슬롯 서수)은 그때마다 지금 텍스처에서 읽으므로 텍스처를 다시
         *          올려도(핫 리로드) 따라간다.
         *          주의: 디스크립터 인덱스를 직접 가지고 있으면 다시 올린 뒤 반환한 슬롯을 읽고, DX11 과 GL 에서는 그 인덱스가 슬롯 서수로 읽혀 엉뚱한
         *          슬롯을 가리킨다.
         */
        void setTextureParameter( hashed_string name, string_view textureAssetPath );
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /** @brief 멀티컴파일 옵션을 선택합니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );

        /** @brief 부모 머티리얼을 반환합니다. */
        Material* getParent() const { return _pParentMaterial; }
        /** @brief 인스턴스 디스크립터를 반환합니다. */
        const MaterialInstanceDef& getDesc() const { return _desc; }
        /** @brief 이름으로 찾은 파라미터 텍스트를 읽습니다. */
        bool getParameter( hashed_string name, string& outValue ) const;
        /** @brief 텍스처 파라미터의 에셋 경로입니다. 덮어썼으면 그 경로, 아니면 부모 프로퍼티의 경로, 둘 다 없으면 빈 문자열입니다. */
        string getTextureParameter( hashed_string name ) const;
        /** @brief 캐시된 셰이더 define 을 반환합니다. */
        const vector<string>& getCachedShaderDefines() const;
        /** @brief 퍼뮤테이션 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief 파라미터가 오버라이드됐으면 true 입니다. */
        bool isParameterOverridden( hashed_string name ) const;
        /** @brief 리플렉션과 파라미터를 대조합니다. */
        bool validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const;

    private:
        /**
         * @struct TextureOverride
         * @brief 텍스처 덮어쓰기 하나입니다. 세터는 `_assetPath` 만 쓰고, `syncTextureOverrides` 가 요청한 것(`_acquiredPath`, `_load`)을 맞춥니다.
         */
        struct TextureOverride
        {
            hashed_string _name;         ///< 텍스처 프로퍼티 이름
            string        _assetPath;    ///< 원하는 텍스처 경로. 비면 지울 덮어쓰기이며 `syncTextureOverrides` 가 놓은 뒤 뺀다.
            string        _acquiredPath; ///< 마지막으로 맞춘 경로. 읽지 못했어도 적어 두어 매 프레임 다시 시도하지 않는다.
            TextureHandle _load{};       ///< 요청한 로드(`TextureCache::request`). 준비되면 참조를 쥐고, 실패하면 이유를 한 번 남기고 놓는다.

            /** @brief 준비된 텍스처입니다. 읽는 중이거나 요청이 없으면 nullptr 입니다(그동안 부모의 텍스처가 보인다). 다시 올려도 객체는 그대로라 SRV 를 그때마다 읽는다. */
            Texture2D* findReadyTexture() const { return _load.isReady() ? _load.getOperation()->getTexture() : nullptr; }
        };

        /** @brief 값 덮어쓰기의 모양입니다. 스칼라 · 벡터는 이진으로 들고 글은 저장 · 조회 때만 만든다. */
        enum class ValueKind : uint8
        {
            Text,   ///< 글 그대로(`setParameter`, XML 에서 읽은 값). 패킹 때 프로퍼티 종류대로 읽는다.
            Scalar, ///< `setScalarParameter`
            Vector  ///< `setVectorParameter`
        };

        /**
         * @struct ValueOverride
         * @brief 값 덮어쓰기 하나입니다.
         */
        struct ValueOverride
        {
            hashed_string _name;
            string        _text;          ///< `ValueKind::Text` 일 때만 쓴다
            float32       _arrValue[4]{}; ///< `Scalar` 는 앞 한 칸, `Vector` 는 네 칸
            ValueKind     _kind{ ValueKind::Text };

            /** @brief float 칸 수입니다. */
            uint32 getFloatCount() const { return _kind == ValueKind::Vector ? 4u : 1u; }
            /** @brief 저장 · 조회용 글입니다. */
            string makeText() const;
        };

        /** @brief 이름의 값 덮어쓰기입니다. 없으면 nullptr 입니다. */
        ValueOverride* findValueOverride( hashed_string name );
        /** @brief float 덮어쓰기를 씁니다. 같은 모양의 같은 값(비트)이면 사본을 낡게 하지 않는다 — 틱마다 값을 쓰는 물 · 식생이 값이 그대로일 때 사본을 다시 만들지 않는다. */
        void assignFloats( hashed_string name, ValueKind kind, const float32* pValue, uint32 count );
        /** @brief 런타임 오버라이드를 Desc 에 다시 씁니다. */
        void syncDescOverrides() const;
        /** @brief 덮어쓰기가 바뀌었다 — 다음 `getRenderBinding` 이 사본을 다시 만들고, 렌더 값의 전역 번호가 오른다(`MaterialRenderVersion`). */
        void markRenderDirty() { _overrideRevision.bump(); }
        /** @brief 부모 바이트에 덮어쓰기를 패킹하고 텍스처 슬롯을 채운 새 사본을 만듭니다(`getRenderBinding`). */
        void rebuildRenderData( IRHIDevice* pDevice );
        /** @brief 부모의 매개변수 모양과 어긋난 스칼라 · 벡터 덮어쓰기를 알리고 지웁니다(`rebuildRenderData` 가 부모 초기화 뒤 게임 스레드에서 부른다). */
        void dropShapeMismatchedOverrides();
        /**
         * @brief 이 인스턴스로 그릴 때의 텍스처 슬롯(DX11 과 GL 의 t16..)에 바인딩할 SRV 를 채웁니다. 부모의 슬롯 위에 덮어쓴 텍스처를 적용합니다.
         * @details 슬롯 바인딩 백엔드는 배치마다 슬롯을 바인딩하고, 인스턴스가 있는 배치는 그 인스턴스의 것이다(합치기는 네이티브 bindless 에서만 켜진다).
         */
        void fillTextureSlots( RHIDescriptorIndex* pOutSlot, uint32 slotCount ) const;
        /**
         * @brief 텍스처 덮어쓰기를 원하는 상태로 맞춥니다(`getRenderBinding`). 바뀐 경로는 반환하고 새로 빌리며, 지운 것은 뺍니다.
         * @return 다시 패킹해야 하면 true 입니다(빌린 것이 바뀌었거나 텍스처가 다시 올라왔습니다).
         */
        bool syncTextureOverrides( IRHIDevice* pRHI );
        /** @brief 요청한 텍스처를 모두 놓습니다(핸들이 쥔 참조를 캐시에 돌려준다). 디바이스가 이미 없으면 캐시가 참조 수만 줄인다(`Material::clearRHI` 와 같다). */
        void releaseTextureOverrides();
        /**
         * @brief 덮어쓴 텍스처의 슬롯 번호입니다. 부모가 그 프로퍼티에 텍스처를 빌렸으면 그 슬롯을, 아니면 부모 슬롯 뒤에 덮어쓰기 순서대로 이어 붙입니다.
         * @return 슬롯 수(`shaderslot::kMaterialTextureCount`)를 넘거나 덮어쓰기가 없으면 비어 있습니다.
         */
        optional<uint32> findTextureSlot( hashed_string name ) const;
        /** @brief XML 텍스트에서 인스턴스를 로드합니다. */
        [[nodiscard]] bool loadFromXML( string_view xmlText );
        /** @brief 정의 캐시가 더러우면 다시 만듭니다(`getCachedShaderDefines` · `getPermutationHash`). */
        void refreshCachedDefines() const;

    private:
        Material*           _pParentMaterial;
        MaterialInstanceDef _desc;

        vector<ValueOverride>               _listValueOverride;
        vector<TextureOverride>             _listTextureOverride;
        vector<pair<hashed_string, bool>>   _listKeywordOverride;
        vector<pair<hashed_string, string>> _listMultiCompileOverride;
        MaterialQualityLevel                _qualityOverride;

        /** @brief 렌더 스레드 쪽 객체(인스턴스 상수 버퍼)입니다. 렌더 스레드만 고친다. */
        shared_ptr<MaterialRenderProxy> _renderProxy;
        /** @brief 마지막으로 만든 불변 사본입니다(`getRenderBinding`). */
        shared_ptr<const MaterialRenderData> _renderData;
        /** @brief 사본을 만들 때의 부모 `getBufferGeneration()` 입니다. 다르면 부모 값이나 레이아웃이 바뀐 것이라 다시 만듭니다. */
        uint32 _parentBufferGeneration;
        /**
         * @brief 덮어쓰기(값 · 텍스처 · 키워드 · 품질 · 정의)가 바뀔 때마다 오르는 번호입니다(`markRenderDirty`). 사본을 만들 때의 값(`_renderDataOverrideRevision`)과 다르면 다시 만든다.
         * @details 예전의 "사본이 낡았다" 깃발 자리다. 번호를 올리는 길이 `bump` 하나라 전역 번호를 빠뜨릴 수 없다(장면 스냅샷이 그 번호로 사본 맞추기를 건너뛴다).
         */
        MaterialRenderVersion _overrideRevision;
        uint32                _renderDataOverrideRevision; ///< `_renderData` 를 만들 때의 `_overrideRevision`
        /** @brief 텍스처 덮어쓰기를 빌린 디바이스입니다. 반환할 때 씁니다(디바이스가 죽으면 clearRHI 가 비웁니다). */
        IRHIDevice* _pTextureDevice;
        /** @brief 덮어쓴 텍스처를 패킹할 때의 `TextureCache::getReloadGeneration()` 입니다. 다르면 다시 올라온 텍스처의 새 SRV 를 다시 패킹합니다. */
        uint32 _textureReloadGeneration;

        mutable vector<string> _listCachedDefine;
        mutable uint64         _cachedPermutationHash;
        mutable uint64         _parentPermutationHash;
        mutable uint8          _bDefinesDirty : 1;
        [[maybe_unused]] uint8 _instReserved  : 7;
    };
} // namespace sw
