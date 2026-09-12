/**
 * @file Material.h
 * @brief XML Material asset + MaterialInstance overrides (UE MIC / Unity MaterialPropertyBlock).
 * @details
 *  - Master Material: property schema + `_defaultValue` + permutations
 *  - MaterialInstance: per-use parameter / keyword / multi_compile overrides → own CB
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw
{
    struct float4;
    struct ShaderCompileResult;
    struct ShaderReflectionData;

    class IRHIDevice;

    /// @brief 셰이더 permutation과 패킹 CB를 가진 머티리얼 에셋
    /*
     * enable_shared_from_this 인 이유: 렌더 패킷(GpuScene 스냅샷)이 이 객체의 **소유를 함께 싣기** 위해서다.
     * 렌더 스레드는 씬을 못 보고 패킷만 받는데, 게임 스레드가 오브젝트를 지우거나 인스턴스를 바꾼 뒤에도
     * 큐에 남은 패킷(링 깊이만큼)이 이 머티리얼을 역참조한다. 소유가 패킷을 따라가면 그 창이 사라진다.
     * 그래서 렌더에 실리는 Material 은 반드시 shared_ptr 로 소유돼야 한다 — 그리고 **반드시 create() 로 만든다.**
     * shared_ptr 의 제어 블록(소멸 코드)은 make_shared 를 부른 쪽 DLL 에 산다. 게임 모듈이 만든 머티리얼을
     * 엔진(GpuScene 스냅샷)이 마지막까지 들고 있다가 모듈이 내려간 뒤 놓으면 이미 없는 코드로 뛰어든다 —
     * 실제로 벤치 종료에서 그렇게 죽었다. create() 는 Engine.dll 안에서 만들므로 누가 마지막에 놓든 안전하다.
     */
    class SW_API Material : public std::enable_shared_from_this<Material>
    {
    public:
        /**
         * @brief 생성 열쇠 — create() 만 만들 수 있다.
         * @details 생성자가 이 열쇠를 요구하므로 `make_shared<Material>()` 도 스택의 `Material x;` 도 **컴파일되지 않는다.**
         *          모든 Material 이 Engine 안에서 shared_ptr 로 태어난다는 것을 컴파일러가 보장한다 — 렌더 패킷이
         *          소유를 빌릴 수 있고, 제어 블록이 모듈 DLL 에 사는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class Material;
        };
        /** @brief create() 전용 생성자 — 열쇠 없이는 만들 수 없다. */
        explicit Material( CreateKey );
        /** @brief 빈 머티리얼을 Engine.dll 안에서 shared_ptr 로 만듭니다. 이것이 Material 을 얻는 유일한 길이다. */
        static shared_ptr<Material> create();
        /** @brief GPU 버퍼와 비동기 로드를 정리합니다. */
        ~Material();

        /** @brief 복사를 금지합니다. */
        Material( const Material& ) = delete;
        /** @brief 대입을 금지합니다. */
        Material& operator=( const Material& ) = delete;
        /** @brief 이동을 금지합니다. */
        Material( Material&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        Material& operator=( Material&& ) = delete;

        /** @brief 머티리얼 에셋을 로드합니다. 경로는 호출측/GameData. */
        bool initialize( IRHIDevice* pRhi, string_view assetRelativePath );
        /** @brief GPU 리소스를 해제합니다. */
        void shutdown( IRHIDevice* pRhi );

        /** @brief 파일에서 머티리얼을 로드합니다. */
        bool loadFromFile( string_view assetRelativePath );
        /** @brief 파일을 비동기로 로드합니다. */
        TaskHandle loadFromFileAsync( string_view assetRelativePath );
        /** @brief 파일로 저장합니다. */
        bool saveToFile( string_view assetRelativePath ) const;
        /** @brief 현재 디스크립터를 XML 문자열로 만듭니다. */
        string saveToString() const;
        /** @brief XML 텍스트에서 머티리얼을 로드합니다. */
        bool loadFromXml( string_view xmlText );
        /** @brief 셰이더 리플렉션에 맞춰 프로퍼티 목록을 맞춥니다. */
        bool syncPropertiesFromReflection( const ShaderReflectionData& reflectionData );
        /**
         * @brief 이 디바이스 백엔드의 셰이더 리플렉션(g_SwMaterials 원소 레이아웃)으로 프로퍼티 오프셋과 원소 stride 를 맞춥니다.
         * @details 백엔드마다 한 번만 한다. GpuScene 이 머티리얼 버퍼를 올리기 전에 부른다 — 레이아웃 정본은 셰이더다.
         * @return 원소 stride 를 얻었으면 true.
         */
        bool ensureShaderLayout( IRHIDevice* pDevice );
        /** @brief 프로퍼티를 패킹 CB에 다시 씁니다. */
        bool rebuildPackedBuffer();
        /** @brief 한 프로퍼티를 `_defaultValue`로 되돌리고 CB를 다시 올립니다. */
        bool resetPropertyToDefault( IRHIDevice* pRhi, hashed_string name );
        /** @brief 모든 프로퍼티를 기본값으로 되돌립니다. */
        void resetAllToDefaults( IRHIDevice* pRhi );
        /**
         * @brief 이 머티리얼 레이아웃으로 외부 버퍼에 값을 패킹합니다.
         * @details MaterialInstance가 마스터를 건드리지 않고 오버라이드 CB를 만들 때 씁니다.
         */
        bool packNamedValueIntoBuffer( hashed_string name, string_view value, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief 텍스처 디스크립터 인덱스를 대상 버퍼에 직접 패킹합니다 (문자열 변환 없음).
         */
        bool packTextureIntoBuffer( hashed_string name, RHIDescriptorIndex descIdx, vector<uint8>& inoutBuffer ) const;

        /**
         * @brief raw POD 바이너리 데이터를 대상 버퍼의 프로퍼티 오프셋에 직접 씁니다.
         */
        bool packRawDataIntoBuffer( hashed_string name, const void* pData, uint32 byteSize, vector<uint8>& inoutBuffer ) const;

        /** @brief 패킹 버퍼 오프셋에 raw 바이트를 씁니다. */
        void setPropertyData( IRHIDevice* pRhi, uint32 offset, uint32 size, const void* pData );
        /** @brief 이름 프로퍼티 값을 텍스트로 설정합니다. */
        bool setPropertyValue( IRHIDevice* pRhi, hashed_string name, string_view value );
        /** @brief 텍스처 슬롯에 디스크립터를 넣습니다. */
        bool setTextureProperty( IRHIDevice* pRhi, hashed_string name, RHIDescriptorIndex descIdx );
        /**
         * @brief Texture2D 프로퍼티 중 assetPath 가 있는 것을 TextureCache 에서 빌려 SRV 인덱스를 패킹합니다.
         * @details initialize 끝에서 부른다(CB 가 있어야 인덱스가 GPU 에 올라간다). shutdown 이 releaseTextureAssets 로 되돌린다.
         */
        void resolveTextureAssets( IRHIDevice* pRhi );
        /** @brief resolveTextureAssets 가 빌린 텍스처를 캐시에 돌려줍니다. */
        void releaseTextureAssets( IRHIDevice* pRhi );
        /**
         * @brief 머티리얼 텍스처의 **백엔드 SRV 인덱스**를 서수 순서로 반환합니다.
         * @details 비네이티브 bindless 백엔드(DX11/GL)에서 엔진이 이 순서대로 t5..t8 에 바인딩한다.
         *          MaterialCB 에 실리는 값은 그 백엔드에선 SRV 인덱스가 아니라 이 배열의 **서수**다.
         */
        const vector<RHIDescriptorIndex>& getMaterialTextureSrvs() const { return _listMaterialTextureSrv; }
        /** @brief 품질 레벨을 설정합니다. */
        void setQualityLevel( MaterialQualityLevel level );
        /** @brief 사용 플래그를 설정합니다. */
        void setUsageFlags( MaterialUsageFlags flags );
        /** @brief 정적 스위치를 켭니다/끕니다. */
        void setStaticSwitch( hashed_string name, bool bEnabled );
        /** @brief 멀티컴파일 옵션을 고릅니다. */
        void setMultiCompile( hashed_string name, string_view selectedOption );
        /** @brief 블렌드 모드를 설정합니다. */
        void setBlendMode( RHIBlendMode mode );
        /** @brief float32 파라미터를 설정합니다. */
        bool setParameterFloat( IRHIDevice* pRhi, hashed_string name, float32 value );

        /** @brief 디스크립터를 반환합니다. */
        const MaterialDesc& getDesc() const { return _desc; }
        /** @brief 디스크립터를 반환합니다. */
        MaterialDesc& getDesc() { return _desc; }
        /** @brief permutation 디스크립터를 반환합니다. */
        const MaterialPermutationDesc& getPermutations() const { return _desc._permutations; }
        /** @brief permutation 디스크립터를 반환합니다. */
        MaterialPermutationDesc& getPermutations() { return _desc._permutations; }
        /** @brief 프로퍼티 목록을 반환합니다. */
        const vector<MaterialProperty>& getProperties() const { return _data._listProperty; }
        /** @brief 패킹된 상수 버퍼를 반환합니다. */
        const vector<uint8>& getBuffer() const { return _data._listBuffer; }
        /** @brief 이름 프로퍼티를 찾습니다. */
        const MaterialProperty* findProperty( hashed_string name ) const;
        /** @brief 이름 프로퍼티를 찾습니다. */
        MaterialProperty* findProperty( hashed_string name );
        /** @brief 프로퍼티 raw 데이터를 반환합니다. */
        const void* getPropertyData( string_view name ) const;
        /** @brief 캐시된 셰이더 define을 반환합니다. */
        const vector<string>& getCachedShaderDefines() const;
        /** @brief 셰이더 키워드 목록을 모읍니다. */
        vector<string> collectShaderKeywords() const { return getCachedShaderDefines(); }
        /** @brief permutation 해시를 반환합니다. */
        uint64 getPermutationHash() const;
        /** @brief 셰이더 경로를 반환합니다. */
        const string& getShaderPath() const { return _desc._shaderPath; }

        /**
         * @brief 셰이더 경로를 바꾸고 경로 해시 캐시를 무효화합니다.
         * @details `getDesc()._shaderPath` 를 직접 쓰면 캐시가 옛 경로를 계속 들고 있다. 경로를
         *          바꾸는 곳은 여기를 쓴다.
         */
        void setShaderPath( string_view shaderPath );

        /**
         * @brief 셰이더 경로의 해시 (경로가 바뀔 때만 다시 계산).
         * @details GpuScene 이 배치 키를 만들 때 인스턴스마다 불린다. 예전엔 그때마다 경로 문자열을
         *          다시 해시했다 — 경로는 머티리얼 수명 동안 거의 안 바뀌는데 프레임마다 인스턴스
         *          수만큼 해시하고 있었다. 정의(define) 해시가 이미 쓰는 더티 플래그 방식과 같다.
         */
        uint64 getShaderPathHash() const;
        /** @brief 머티리얼 이름을 반환합니다. */
        const string& getName() const { return _desc._name; }
        /** @brief bindless 디스크립터 인덱스를 반환합니다. */
        RHIDescriptorIndex getDescriptorIndex() const { return _descriptorIndex; }
        /** @brief 상수 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getConstantBuffer() const { return _constantBuffer; }
        /**
         * @brief GPUScene 머티리얼 데이터 버퍼(g_SwMaterials)의 원소 stride — 셰이더 리플렉션의 구조버퍼 원소 크기.
         * @details 0 이면 셰이더가 SW_MATERIAL_BEGIN/END 를 쓰지 않는다(레거시 MaterialCB). 백엔드마다 다를 수 있다.
         */
        uint32 getElementStride() const { return _elementStride; }
        /** @brief 블렌드 모드를 반환합니다. */
        RHIBlendMode getBlendMode() const { return _blendMode; }
        /** @brief float32 파라미터를 읽습니다. */
        bool getParameterFloat( hashed_string name, float32& outValue ) const;

    private:
        /// @brief 비동기 셰이더 컴파일 진행 상태
        struct AsyncLoadState
        {
            mutex     _mutex;
            Material* _pMaterial{ nullptr };
        };

        /** @brief Desc를 런타임 상태에 적용합니다. */
        void applyDescToRuntime();
        /** @brief 런타임 프로퍼티를 Desc에 다시 씁니다 (저장용). */
        void syncDescFromRuntime() const;
        /** @brief TaskArgs: AsyncLoadState shared_ptr, path string. */
        static void loadFromFileAsyncJob( const TaskArgs& args );

        MaterialDesc               _desc;
        MaterialData               _data;
        RHIBufferHandle            _constantBuffer;
        RHIDescriptorIndex         _descriptorIndex;
        uint32                     _elementStride;                /**< g_SwMaterials 원소 stride (리플렉션). 0 = 구조버퍼 머티리얼 아님 */
        uint32                     _shaderLayoutBackendMask{ 0 }; /**< ensureShaderLayout 을 끝낸 백엔드 비트 */
        IRHIDevice*                _pRHIDevice;
        vector<string>             _listAcquiredTexturePath; ///< resolveTextureAssets 가 빌린 경로 — shutdown 때 그대로 돌려준다
        vector<RHIDescriptorIndex> _listMaterialTextureSrv;  ///< 위 경로와 같은 순서의 백엔드 SRV 인덱스(에뮬 백엔드 슬롯 바인딩용)
        RHIBlendMode               _blendMode;
        shared_ptr<AsyncLoadState> _asyncLoadState;

        mutable vector<string> _listCachedDefine;
        mutable uint64         _cachedPermutationHash;
        mutable uint64         _cachedShaderPathHash;
        mutable uint8          _bDefinesDirty        : 1;
        mutable uint8          _bShaderPathHashDirty : 1;
        [[maybe_unused]] uint8 _reservedMaterial     : 6;
    };
} // namespace sw
