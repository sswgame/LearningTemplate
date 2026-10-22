/**
 * @file Material.h
 * @brief XML 머티리얼 에셋과 MaterialInstance 오버라이드를 정의합니다(UE 의 MIC, Unity 의 MaterialPropertyBlock 에 해당).
 * @details
 *  - 마스터 머티리얼은 프로퍼티 스키마, `_defaultValue`, 퍼뮤테이션을 가진다.
 *  - MaterialInstance 는 사용처마다 파라미터, 키워드, multi_compile 을 덮어쓰고 자기 CB 를 가진다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    enum class RHIBackend : uint32;
    enum class ShaderTargetFormat : uint8;

    struct float4;
    struct ShaderBufferInfo;
    struct ShaderCompileResult;
    struct ShaderReflectionData;

    class IRHIDevice;

    /// @brief 셰이더 퍼뮤테이션과 패킹 CB 를 가진 머티리얼 에셋입니다.
    /*
     * enable_shared_from_this 인 이유는 렌더 패킷(GPUScene 스냅샷)이 이 객체의 **소유권을 함께 싣기** 위해서다.
     * 렌더 스레드는 씬을 못 보고 패킷만 받는데, 게임 스레드가 오브젝트를 지우거나 인스턴스를 바꾼 뒤에도
     * 큐에 남은 패킷(링 깊이만큼)이 이 머티리얼을 역참조한다. 소유권이 패킷을 따라가면 그 구간이 사라진다.
     * 그래서 렌더에 실리는 Material 은 반드시 shared_ptr 로 소유되어야 하고, **반드시 create() 로 만든다.**
     * shared_ptr 의 제어 블록(소멸 코드)은 make_shared 를 부른 쪽 DLL 에 있다. 게임 모듈이 만든 머티리얼을
     * 엔진(GPUScene 스냅샷)이 마지막까지 가지고 있다가 모듈이 언로드된 뒤 놓으면 이미 없는 코드로 뛰어든다.
     * create() 는 Engine.dll 안에서 만들므로 누가 마지막에 놓든 안전하다.
     */
    class SW_API Material final : public RHIRenderResource, public std::enable_shared_from_this<Material>
    {
    public:
        /** @brief 머티리얼 파일의 확장자입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".material";
        /** @brief 머티리얼 파일의 접미사입니다(쿠킹하지 않는다). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 머티리얼 에셋 종류입니다(등록은 `MaterialCache.cpp` 의 `SW_ASSET_TYPE`). 판 0. */
        static constexpr AssetTypeDef kAssetType{ "Material", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        /**
         * @brief 생성 키입니다. create() 만 만들 수 있습니다.
         * @details 생성자가 이 키를 요구하므로 `make_shared<Material>()` 도 스택의 `Material x;` 도 **컴파일되지 않는다.**
         *          모든 Material 이 Engine 안에서 shared_ptr 로 만들어진다는 것을 컴파일러가 보장한다. 그래서 렌더 패킷이
         *          소유권을 가질 수 있고, 제어 블록이 모듈 DLL 에 있는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class Material;
        };
        /** @brief create() 전용 생성자입니다. 키 없이는 만들 수 없습니다. */
        explicit Material( CreateKey );
        /** @brief 빈 머티리얼을 Engine.dll 안에서 shared_ptr 로 만듭니다. Material 을 얻는 유일한 방법입니다. */
        static shared_ptr<Material> create();
        /** @brief GPU 버퍼와 비동기 로드를 정리합니다. */
        ~Material() override;

        /** @brief 복사를 금지합니다. */
        Material( const Material& ) = delete;
        /** @brief 대입을 금지합니다. */
        Material& operator=( const Material& ) = delete;
        /** @brief 이동을 금지합니다. */
        Material( Material&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        Material& operator=( Material&& ) = delete;

        /** @brief 머티리얼 에셋을 로드하고 GPU 자원(상수 버퍼, 텍스처)을 올립니다. */
        bool initialize( IRHIDevice* pRHI, string_view assetRelativePath );
        /** @brief (RHIRenderResource) 살아 있는 디바이스에 GPU 자원을 반환합니다. */
        void releaseRHI( IRHIDevice* pRHI ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 버립니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 새 디바이스에 상수 버퍼와 텍스처를 다시 올립니다. */
        bool initRHI( IRHIDevice* pDevice ) override;
        /** @brief GPU 자원이 올라가 있으면 true 입니다. 캐시가 따로 세지 않고 이 값을 봅니다. */
        bool isRHIValid() const { return _constantBuffer != 0; }

        /** @brief 파일에서 머티리얼을 로드합니다. */
        [[nodiscard]] bool loadFromFile( string_view assetRelativePath );
        /**
         * @brief 디스크 파일에서 머티리얼을 로드합니다. 리소스 id 로 풀지 않고 팩을 보지 않습니다(`XMLDocument::loadFile`).
         * @details 소스 트리를 디스크로 훑은 쿠커 · 테스트용입니다 — 리소스 루트 안 절대 경로를 `loadFromFile` 에 넣으면 팩 전용 구성이 거절합니다.
         */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 파일을 비동기로 로드합니다. */
        TaskHandle loadFromFileAsync( string_view assetRelativePath );
        /** @brief 파일로 저장합니다. */
        [[nodiscard]] bool saveToFile( string_view assetRelativePath ) const;
        /** @brief 현재 디스크립터를 XML 문자열로 만듭니다. */
        string saveToString() const;
        /** @brief XML 텍스트에서 머티리얼을 로드합니다. */
        [[nodiscard]] bool loadFromXML( string_view xmlText );
        /**
         * @brief 셰이더 리플렉션에 맞춰 프로퍼티 목록을 조정합니다.
         * @return 모든 프로퍼티를 셰이더 칸에 맞췄고, 셰이더 구조체의 필드를 머티리얼이 모두 적었으면 true 입니다. 스키마가 없거나 적지 않은 필드가 있으면
         *         오류를 남기고 false 입니다 — 적지 않은 칸을 0 으로 두고 넘어가지 않는다(텍스처 인덱스의 0 은 첫 실제 텍스처다).
         */
        [[nodiscard]] bool syncPropertiesFromReflection( const ShaderReflectionData& reflectionData );
        /** @brief 리플렉션에 머티리얼 스키마(`g_SwMaterials` 원소 또는 MaterialCB)가 있으면 true 입니다. */
        static bool hasMaterialSchema( const ShaderReflectionData& reflectionData );
        /** @brief 셰이더의 머티리얼 스키마가 든 리플렉션을 구합니다(픽셀 스테이지, 없으면 정점 스테이지). 얻지 못하면 false 입니다. `ensureShaderLayout` 과 로드 검증이 함께 씁니다. */
        [[nodiscard]] static bool reflectMaterialSchema( const string& shaderPath, ShaderTargetFormat targetFormat, ShaderReflectionData& outReflection );
        /**
         * @brief 이 디바이스 백엔드의 셰이더 리플렉션(g_SwMaterials 원소 레이아웃)으로 프로퍼티 오프셋과 원소 stride 를 맞춥니다.
         * @details 이미 맞춰져 있으면(`isShaderLayoutSynced`) 아무것도 하지 않는다. GPUScene 과 인스턴스(`MaterialInstance::updateRHI`)가 바이트를
         *          읽기 전에 부른다. 레이아웃의 기준은 셰이더다.
         * @return 원소 stride 를 얻었고 동기화가 성공했으면(`syncPropertiesFromReflection`) true 입니다.
         */
        [[nodiscard]] bool ensureShaderLayout( IRHIDevice* pDevice );
        /**
         * @brief 프로퍼티 오프셋과 stride 가 지금 이 백엔드의 셰이더 레이아웃에 맞춰져 있는지 나타냅니다.
         * @details 다시 로드하면(`loadFromXML`, 에셋 핫 리로드, 에디터 미리보기) XML 순서로 다시 쌓이므로 풀리고, 셰이더 리플렉션 캐시가
         *          비워지면(다시 쿠킹, 라이브 셰이더 편집) 낡은 값이 된다. 마지막으로 맞춘 백엔드와 다른 백엔드로 물어도 false 다.
         *          리플렉션을 얻지 못한 시도도 "맞춰 봤다" 로 기록한다. 매 프레임 같은 오류를 내지 않기 위해서다.
         */
        bool isShaderLayoutSynced( RHIBackend backend ) const;
        /** @brief 프로퍼티를 패킹 CB 에 다시 씁니다. */
        bool rebuildPackedBuffer();
        /**
         * @brief 프로퍼티의 오프셋과 크기대로 바이트 버퍼를 다시 채웁니다. 리플렉션 동기화의 두 분기가 같은 경로를 씁니다.
         * @param minimumByteSize 셰이더가 선언한 크기입니다. 프로퍼티가 그 밖으로 나가면 그만큼 늘리고, 끝은 256 으로 정렬합니다.
         */
        void packPropertiesIntoBytes( uint32 minimumByteSize );
        /** @brief 한 프로퍼티를 `_defaultValue` 로 되돌리고 CB 를 다시 올립니다. */
        bool resetParameterToDefault( IRHIDevice* pRHI, hashed_string name );
        /**
         * @brief 이 머티리얼 레이아웃으로 외부 버퍼에 값을 패킹합니다.
         * @details MaterialInstance 가 마스터를 건드리지 않고 오버라이드 CB 를 만들 때 쓴다.
         */
        bool packNamedValueIntoBuffer( hashed_string name, string_view value, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief 텍스처 디스크립터 인덱스를 대상 버퍼에 바로 패킹합니다(문자열 변환 없음).
         */
        bool packTextureIntoBuffer( hashed_string name, RHIDescriptorIndex descIdx, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief POD 바이너리 데이터를 대상 버퍼의 프로퍼티 오프셋에 바로 씁니다.
         */
        bool packRawDataIntoBuffer( hashed_string name, const void* pData, uint32 byteSize, vector<uint8>& inoutBuffer ) const;

        /** @brief 이름으로 찾은 프로퍼티 값을 텍스트로 설정합니다. */
        bool setParameter( IRHIDevice* pRHI, hashed_string name, string_view value );
        /** @brief 텍스처 슬롯에 디스크립터를 설정합니다. */
        bool setTextureParameter( IRHIDevice* pRHI, hashed_string name, RHIDescriptorIndex descIdx );
        /**
         * @brief Texture2D 프로퍼티 중 assetPath 가 있는 것을 TextureCache 에서 빌려 SRV 인덱스를 패킹합니다.
         * @details initialize 끝에서 부른다(CB 가 있어야 인덱스가 GPU 에 올라간다). releaseRHI 와 forgetRHI 가 releaseTextureAssets 로 되돌린다.
         *          못 읽은 텍스처는 엔진의 누락 텍스처 체커를 빌린다. 체커마저 못 읽으면 오류를 남기고 false 다(흰색으로 대신하지 않는다).
         */
        [[nodiscard]] bool resolveTextureAssets( IRHIDevice* pRHI );
        /**
         * @brief 텍스처 로드를 어디서 시작할지 정합니다(다음 `initialize` · `initRHI` 부터). 기본은 그 자리(`AssetLoadStart::OnCallingThread`)입니다.
         * @details 씬이 쓰는 머티리얼(`MaterialCache`)은 `Async` 로 둔다 — 텍스처를 워커가 읽는 동안 로드 중 텍스처(회색)를 그린다. 직접 만든 머티리얼(도구 · 테스트 ·
         *          에디터 미리보기)은 그 자리에서 읽어 첫 그림부터 진짜 텍스처다.
         */
        void setTextureLoadStart( AssetLoadStart start ) { _textureLoadStart = start; }
        /** @brief resolveTextureAssets 가 빌린 텍스처를 캐시에 반환합니다. */
        void releaseTextureAssets( IRHIDevice* pRHI );
        /**
         * @brief 빌린 텍스처가 다시 업로드되었거나(`TextureCache::reload`) 비동기 로드가 끝났으면 새 SRV 인덱스를 받아 다시 패킹합니다.
         * @details 로드 중인 텍스처 자리는 로드 중 텍스처(`EngineDefaultAssets::_loadingTexture`)를 샘플하다가, 준비되면 진짜로, 실패하면 누락 텍스처(마젠타)로 바꾼다.
         * @details 텍스처 핫 리로드는 같은 `Texture2D` 에 새 텍스처와 새 SRV 인덱스를 올리고 옛 인덱스는 반환한다. resolve 때 받은
         *          인덱스를 바이트(네이티브 bindless)와 슬롯 목록(DX11, GL)에 그대로 가지고 있으면 **반환한 슬롯**을 읽는다. 캐시의 reload
         *          세대가 그대로면 아무것도 하지 않으므로 매 빌드 불러도 싸다(`GPUSceneBuilder` 는 세대가 바뀐 빌드에서만 부른다).
         * @return 인덱스가 하나라도 바뀌었으면 true 입니다.
         */
        bool refreshTextureBindings();
        /**
         * @brief 머티리얼 텍스처의 **백엔드 SRV 인덱스**를 서수 순서로 반환합니다.
         * @details 네이티브 bindless 가 없는 백엔드(DX11, GL)에서 엔진이 이 순서대로 t16..t31 에 바인딩한다.
         *          그 백엔드에서 MaterialCB 에 실리는 값은 SRV 인덱스가 아니라 이 배열의 **서수**다.
         */
        const vector<RHIDescriptorIndex>& getMaterialTextureSrvs() const { return _listMaterialTextureSrv; }
        /** @brief 빌린 텍스처가 없는 프로퍼티의 슬롯 값입니다(`findTextureSlot`). */
        static constexpr uint32 kInvalidTextureSlot = invalid_index::kUint32;
        /**
         * @brief 텍스처 프로퍼티 `name` 이 슬롯 목록(`getMaterialTextureSrvs`)의 몇 번째인지 반환합니다. 이 머티리얼이 그 프로퍼티에 텍스처를 빌리지
         *        않았으면 kInvalidTextureSlot 입니다.
         * @details 인스턴스가 같은 프로퍼티의 텍스처를 덮어쓸 때 같은 슬롯을 쓰려고 묻는다(`MaterialInstance::findTextureSlot`).
         */
        uint32 findTextureSlot( hashed_string name ) const;
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /**
         * @brief 정적 스위치를 켜거나 끕니다.
         * @note Shipping 에서 런타임에 바꿔도 되는 것은 `bShaderFeature="0"` 인 스위치뿐이다(켬과 끔 둘 다 쿠킹된다). 나머지는 에셋 상태만 쿠킹되어
         *       바꾼 변형의 바이너리가 없다. Dev 는 실시간 컴파일이 가린다(`ShaderCookRequestTest.EveryRuntimeStaticSwitchCombinationIsRequested`).
         */
        void setStaticSwitch( hashed_string name, bool bEnabled );
        /** @brief 멀티컴파일 옵션을 선택합니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );
        /** @brief 블렌드 모드를 설정합니다. */
        void setBlendMode( RHIBlendMode mode );
        /** @brief float32 파라미터를 설정합니다. */
        bool setScalarParameter( IRHIDevice* pRHI, hashed_string name, float32 value );

        /** @brief 디스크립터를 반환합니다. */
        const MaterialDesc& getDesc() const { return _desc; }
        /** @brief 디스크립터를 반환합니다. */
        MaterialDesc& getDesc() { return _desc; }
        /** @brief 퍼뮤테이션 디스크립터를 반환합니다. */
        const MaterialPermutationDesc& getPermutations() const { return _desc._permutations; }
        /** @brief 퍼뮤테이션 디스크립터를 반환합니다. */
        MaterialPermutationDesc& getPermutations() { return _desc._permutations; }
        /** @brief 프로퍼티 목록을 반환합니다. */
        const vector<MaterialProperty>& getProperties() const { return _data._listProperty; }
        /** @brief 패킹된 상수 버퍼를 반환합니다. */
        const vector<uint8>& getBuffer() const { return _data._bytes; }
        /**
         * @brief `getBuffer()` 의 바이트가 바뀔 때마다 오르는 세대입니다(값, 레이아웃, 크기).
         * @details 인스턴스는 부모 바이트의 복사본을 가지고 있어, 이 값이 바뀌면 다시 복사한다(`MaterialInstance::updateRHI`).
         */
        uint32 getBufferGeneration() const { return _bufferGeneration; }
        /** @brief 이름으로 프로퍼티를 찾습니다. */
        const MaterialProperty* findProperty( hashed_string name ) const;
        /** @brief 이름으로 프로퍼티를 찾습니다. */
        MaterialProperty* findProperty( hashed_string name );
        /** @brief 프로퍼티의 원시 데이터를 반환합니다. */
        const void* getParameterData( string_view name ) const;
        /** @brief 캐시된 셰이더 define 을 반환합니다. */
        const vector<string>& getCachedShaderDefines() const;
        /** @brief 퍼뮤테이션 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief 셰이더 경로를 반환합니다. */
        const string& getShaderPath() const { return _desc._shaderPath; }

        /**
         * @brief 셰이더 경로를 바꾸고 경로 해시 캐시를 무효화합니다.
         * @details `getDesc()._shaderPath` 를 직접 쓰면 캐시가 옛 경로를 계속 가지고 있다. 경로를
         *          바꾸는 곳은 여기를 쓴다.
         */
        void setShaderPath( string_view shaderPath );

        /**
         * @brief 셰이더 경로의 해시입니다(경로가 바뀔 때만 다시 계산합니다).
         * @details GPUScene 이 배치 키를 만들 때 인스턴스마다 부른다. 경로는 머티리얼 수명 동안 거의 바뀌지 않으므로
         *          정의(define) 해시와 같은 더티 플래그 방식으로 캐시한다.
         */
        uint64 getShaderPathHash() const;
        /** @brief 머티리얼 이름을 반환합니다. */
        const string& getName() const { return _desc._name; }
        /** @brief bindless 디스크립터 인덱스를 반환합니다. */
        RHIDescriptorIndex getDescriptorIndex() const { return _descriptorIndex; }
        /** @brief 상수 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getConstantBuffer() const { return _constantBuffer; }
        /**
         * @brief GPUScene 머티리얼 데이터 버퍼(g_SwMaterials)의 원소 stride 입니다. 셰이더 리플렉션의 구조 버퍼 원소 크기입니다.
         * @details 0 이면 셰이더가 SW_MATERIAL_BEGIN/END 를 쓰지 않는다(레거시 MaterialCB). 백엔드마다 다를 수 있다.
         */
        uint32 getElementStride() const { return _elementStride; }
        /** @brief 블렌드 모드를 반환합니다. */
        RHIBlendMode getBlendMode() const { return _blendMode; }
        /** @brief float32 파라미터를 읽습니다. */
        bool getScalarParameter( hashed_string name, float32& outValue ) const;

    private:
        /** @brief 프로퍼티가 없는 머티리얼에 리플렉션 스키마의 멤버를 프로퍼티로 채우고(텍스처 인덱스 멤버는 텍스처 속성) 바이트를 쌓습니다. */
        void fillPropertiesFromSchema( const ShaderBufferInfo& schema );
        /** @brief 있는 프로퍼티를 스키마 멤버에 맞춰 오프셋 · 크기 · 셰이더 타입을 받습니다. 없거나 크기가 맞지 않는 프로퍼티가 있으면 false 입니다. */
        [[nodiscard]] bool matchPropertiesToSchema( const ShaderBufferInfo& schema );
        /** @brief 레이아웃 오류 뒤 바이트와 stride 를 비운다 — 틀린 바이트(적지 않은 칸의 0)가 GPU 에 올라가지 않게 한다. */
        void discardBytesAfterLayoutError();

        /// @brief 비동기 파일 로드가 머티리얼이 아직 살아 있는지 확인하는 상태입니다. 소멸자가 포인터를 끊습니다.
        struct AsyncLoadState
        {
            mutex     _mutex;
            Material* _pMaterial{ nullptr };
        };

        /** @brief Desc 를 런타임 상태에 적용합니다. */
        void applyDescToRuntime();
        /** @brief 서수 @p ordinal 의 텍스처 로드가 끝났으면 대신 빌린 텍스처를 놓고 진짜(실패면 누락 체커)로 바꿉니다(`refreshTextureBindings`). */
        void resolveArrivedTexture( TextureCache& textures, size_t ordinal );
        /** @brief 런타임 프로퍼티를 Desc 에 다시 씁니다(저장용). */
        void syncDescFromRuntime() const;
        /** @brief 비동기 로드 태스크 본문입니다. TaskArgs 에는 AsyncLoadState shared_ptr 와 경로 문자열이 들어 있습니다. */
        static void loadFromFileAsyncJob( const TaskArgs& args );

        /** @brief initialize 가 받은 에셋 경로입니다. 디바이스가 바뀌면 이 경로만으로 스스로 복원합니다. */
        string                     _assetPath;
        MaterialDesc               _desc;
        MaterialData               _data;
        RHIBufferHandle            _constantBuffer;
        RHIDescriptorIndex         _descriptorIndex;
        uint32                     _elementStride;               /**< g_SwMaterials 원소 stride(리플렉션). 0이면 구조 버퍼 머티리얼이 아니다. */
        uint32                     _bufferGeneration;            /**< `_data._bytes` 가 바뀔 때마다 오른다. 인스턴스가 복사본이 낡았는지 확인한다. */
        uint32                     _shaderLayoutCacheGeneration; /**< 레이아웃을 맞출 때의 ShaderReflectionLibrary 캐시 세대 */
        RHIBackend                 _shaderLayoutBackend;         /**< 레이아웃을 맞춘 백엔드. `_bShaderLayoutSynced` 일 때만 의미가 있다. */
        IRHIDevice*                _pRHIDevice;
        uint32                     _textureReloadGeneration; ///< SRV 인덱스를 받을 때의 `TextureCache::getReloadGeneration()`
        vector<string>             _listAcquiredTexturePath; ///< resolveTextureAssets 가 해석한 요청 경로(프로퍼티의 assetPath). refreshTextureBindings 가 프로퍼티와 맞춘다.
        vector<TextureHandle>      _listTextureLoad;         ///< 위와 같은 순서의 텍스처 로드(`TextureCache::request`). 준비되면 참조를 쥐고, 실패하면 놓는다
        AssetLoadStart             _textureLoadStart;        ///< 텍스처 로드를 어디서 시작하는가(`setTextureLoadStart`). 기본은 그 자리(동기)
        vector<string>             _listBorrowedTexturePath; ///< 위와 같은 순서로 대신 빌린 경로(`acquire` · `release` 짝) — 로드 중이면 로드 중 텍스처, 실패면 누락 텍스처, 진짜가 붙었으면 빈 문자열
        vector<RHIDescriptorIndex> _listMaterialTextureSrv;  ///< 위 경로와 같은 순서의 백엔드 SRV 인덱스(에뮬레이션 백엔드의 슬롯 바인딩용)
        vector<hashed_string>      _listMaterialTextureName; ///< 위와 같은 순서의 프로퍼티 이름(인스턴스가 같은 슬롯을 덮어쓴다)
        RHIBlendMode               _blendMode;
        shared_ptr<AsyncLoadState> _asyncLoadState;

        mutable vector<string> _listCachedDefine;
        mutable uint64         _cachedPermutationHash;
        mutable uint64         _cachedShaderPathHash;
        mutable uint8          _bDefinesDirty        : 1;
        mutable uint8          _bShaderPathHashDirty : 1;
        uint8                  _bShaderLayoutSynced  : 1; /**< ensureShaderLayout 이 맞췄다(백엔드와 캐시 세대와 함께 본다). 다시 로드하면 풀린다. */
        [[maybe_unused]] uint8 _reservedMaterial     : 5;
    };
} // namespace sw
