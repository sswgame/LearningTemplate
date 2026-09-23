/**
 * @file ShaderBindingLayout.h
 * @brief 여러 스테이지의 ShaderReflectionData 를 합쳐 이름 · 레지스터로 조회할 수 있는 바인딩 레이아웃을 만듭니다.
 * @details "셰이더만 고치면 된다" 를 떠받치는 핵심입니다. C++ 미러 struct 없이 런타임 리플렉션만 믿습니다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

namespace sw
{
    /// @brief 슬롯이 가리키는 리소스 종류입니다.
    enum class ShaderBindingKind : uint8
    {
        ConstantBuffer,
        Texture,
        Sampler,
        StructuredBuffer,
        RwStructuredBuffer,
        RwTexture,
        Unknown
    };

    /// @brief 합쳐진 바인딩 슬롯 하나입니다(이름 + 레지스터 + 가시성 + CB 멤버).
    struct ShaderBindingSlot
    {
        hashed_string              _name;
        ShaderBindingKind          _kind{ ShaderBindingKind::Unknown };
        uint32                     _space{ 0 };
        uint32                     _registerIndex{ 0 };
        uint32                     _arrayCount{ 1 };
        ShaderStageFlag            _visibility{ ShaderStageFlag::None };
        vector<ShaderVariableInfo> _listCbMember; ///< kind==ConstantBuffer 일 때만 채움
        uint32                     _cbTotalSize{ 0 };
        /**
         * @brief StructuredBuffer<T> 원소 하나의 바이트 수입니다(kind==StructuredBuffer/RwStructuredBuffer 일 때만, 모르면 0).
         * @details 셰이더가 선언한 stride 입니다. 이 슬롯에 거는 버퍼는 같은 stride 로 만들어야 합니다. DX11 은 SRV 의 구조
         *          stride 가 셰이더 선언과 다르면 디버그 레이어가 드로우마다 오류를 내고(값도 보장되지 않습니다), DX12 는
         *          디스크립터 테이블의 StructuredBuffer SRV 가 같은 제약을 갖습니다. 원래 값은 백엔드마다 다를 수 있습니다
         *          (DX 자연 패킹 / SPIR-V std430). 지금은 SPIR-V 도 `-fvk-use-dx-layout` 으로 DX 규칙을 따르지만,
         *          레이아웃은 여전히 디바이스 백엔드로 빌드한 것을 씁니다.
         */
        uint32 _elementStride{ 0 };
    };

    /**
     * @struct ShaderEngineCbMember
     * @brief 엔진 CB 멤버 하나를 채우는 데 필요한 것입니다. **드로우 전에 미리 구워 둡니다.**
     * @details 예전에는 드로우마다 멤버 이름으로 `hashed_string` 을 만들고(전역 intern 테이블 조회),
     *          `"Index"` 부분 문자열을 찾고, canonical 이름을 `string` 으로 새로 할당했습니다. 모두
     *          레이아웃에만 달린 값이라 PSO 마다 한 번이면 충분합니다.
     */
    struct ShaderEngineCbMember
    {
        hashed_string _valueKey;     ///< PassConstantValues 조회 키(= 멤버 이름)
        hashed_string _autoIndexKey; ///< 값이 없을 때 레지스트리에서 찾을 canonical 이름. 비어 있으면 자동 채움 대상이 아님
        uint32        _offset{ 0 };
        uint32        _size{ 0 };
    };

    /**
     * @struct ShaderResourceBind
     * @brief 텍스처 · 구조버퍼 슬롯 하나의 바인딩에 필요한 것입니다. 마찬가지로 미리 구워 둡니다.
     */
    struct ShaderResourceBind
    {
        hashed_string     _lookupKey; ///< 레지스트리 조회 키(canonical 이름)
        uint32            _registerIndex{ 0 };
        ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
    };

    /**
     * @class ShaderBindingLayout
     * @brief PSO 하나(VS+PS 혹은 CS)의 합쳐진 바인딩 레이아웃입니다. 이름 · 레지스터로 조회합니다.
     */
    class SW_API ShaderBindingLayout
    {
    public:
        /** @brief 스테이지별 리플렉션 데이터를 합쳐 레이아웃을 만듭니다. */
        static ShaderBindingLayout build( const vector<pair<ShaderStage, const ShaderReflectionData*>>& listStageReflection );

        /// @brief 리플렉션 문자열 타입 라벨("Texture", "StorageBuffer", …)을 ShaderBindingKind 로 바꿉니다. 계약 검증기도 같은 분류를 씁니다.
        static ShaderBindingKind kindFromTypeLabel( string_view typeLabel );

        /** @brief 이름으로 슬롯을 찾습니다(없으면 nullptr). */
        const ShaderBindingSlot* find( hashed_string name ) const;

        /** @brief 전체 슬롯 목록입니다. */
        const vector<ShaderBindingSlot>& getSlots() const { return _listSlot; }
        /** @brief 슬롯이 하나도 없으면 true 입니다(컴파일 · 리플렉션 실패 등). */
        bool isEmpty() const { return _listSlot.empty(); }

        // ------------------------------------------------------------------------------
        // 드로우 경로용 사전 계산: buildBindPlan() 이 한 번만 채운다
        // ------------------------------------------------------------------------------
        /** @brief 엔진 CB(= Material 이 아닌 CB) 전체를 담는 데 필요한 바이트 수입니다. */
        uint32 getEngineCbSize() const { return _engineCbSize; }
        /** @brief 엔진 CB 를 채울 멤버 표입니다. 드로우마다 이름을 다시 해시하지 않기 위한 것입니다. */
        const vector<ShaderEngineCbMember>& getEngineCbMembers() const { return _listEngineCbMember; }
        /** @brief 텍스처 · 구조버퍼 슬롯 바인딩 표입니다. */
        const vector<ShaderResourceBind>& getResourceBinds() const { return _listResourceBind; }

    private:
        void rebuildIndex();
        /** @brief 드로우마다 반복하던 계산을 여기서 한 번만 합니다(build 끝에서 부릅니다). */
        void buildBindPlan();

        vector<ShaderBindingSlot>            _listSlot;
        unordered_map<hashed_string, uint32> _mapNameToSlot;

        uint32                       _engineCbSize{ 0 };
        vector<ShaderEngineCbMember> _listEngineCbMember;
        vector<ShaderResourceBind>   _listResourceBind;
    };
} // namespace sw
