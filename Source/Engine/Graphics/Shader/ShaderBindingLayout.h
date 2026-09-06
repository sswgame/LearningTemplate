/**
 * @file ShaderBindingLayout.h
 * @brief 여러 스테이지의 ShaderReflectionData 를 병합해 이름/레지스터로 조회 가능한 바인딩 레이아웃을 만든다.
 * @details "셰이더만 고치면 된다" 를 지탱하는 핵심. C++ 미러 struct 없이 런타임 리플렉션만 신뢰한다.
 */
#pragma once
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"

namespace sw
{
    /// @brief 슬롯이 가리키는 리소스 종류.
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

    /// @brief 병합된 바인딩 슬롯 하나 (이름 + 레지스터 + 가시성 + CB 멤버).
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
    };

    /**
     * @struct ShaderEngineCbMember
     * @brief 엔진 CB 멤버 하나를 채우는 데 필요한 것 — **드로우 전에 미리 구워 둔다.**
     * @details 예전엔 드로우마다 멤버 이름으로 `hashed_string` 을 만들고(전역 intern 테이블 조회),
     *          `"Index"` 부분문자열을 찾고, canonical 이름을 `string` 으로 새로 할당했다. 전부
     *          레이아웃만의 함수라 PSO 마다 한 번이면 충분하다.
     */
    struct ShaderEngineCbMember
    {
        hashed_string _valueKey;     ///< PassConstantValues 조회 키 (= 멤버 이름)
        hashed_string _autoIndexKey; ///< 값이 없을 때 레지스트리에서 찾을 canonical 이름. 비어 있으면 자동 채움 대상이 아니다
        uint32        _offset{ 0 };
        uint32        _size{ 0 };
    };

    /**
     * @struct ShaderResourceBind
     * @brief 텍스처/구조버퍼 슬롯 하나의 바인딩에 필요한 것 — 마찬가지로 미리 구워 둔다.
     */
    struct ShaderResourceBind
    {
        hashed_string     _lookupKey; ///< 레지스트리 조회 키 (canonical 이름)
        uint32            _registerIndex{ 0 };
        ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
    };

    /**
     * @class ShaderBindingLayout
     * @brief PSO 하나(VS+PS 혹은 CS)의 병합 바인딩 레이아웃. 이름/레지스터로 조회한다.
     */
    class SW_API ShaderBindingLayout
    {
    public:
        /** @brief 스테이지별 리플렉션 데이터를 병합해 레이아웃을 만든다. */
        static ShaderBindingLayout build( const vector<pair<ShaderStage, const ShaderReflectionData*>>& listStageReflection );

        /** @brief 이름으로 슬롯을 찾는다 (없으면 nullptr). */
        const ShaderBindingSlot* find( hashed_string name ) const;
        /** @brief (종류, space, register) 로 슬롯을 찾는다. */
        const ShaderBindingSlot* findByRegister( ShaderBindingKind kind, uint32 space, uint32 registerIndex ) const;
        /** @brief CB 이름 + 멤버 이름으로 오프셋/크기를 찾는다. 없으면 false. */
        bool resolveCbMember( hashed_string cbName, hashed_string memberName, uint32& outOffset, uint32& outSize ) const;

        /** @brief 전체 슬롯 목록. */
        const vector<ShaderBindingSlot>& getSlots() const { return _listSlot; }
        /** @brief 슬롯이 하나도 없으면 true (컴파일/리플렉션 실패 등). */
        bool isEmpty() const { return _listSlot.empty(); }
        /** @brief 레이아웃 내용 핑거프린트 (핫리로드 변경 감지용). */
        uint64 fingerprint() const { return _fingerprint; }

        // ------------------------------------------------------------------------------
        // 드로우 경로용 사전 계산 — buildBindPlan() 이 한 번만 채운다
        // ------------------------------------------------------------------------------
        /** @brief 엔진 CB(= Material 이 아닌 CB) 전체를 담는 데 필요한 바이트 수. */
        uint32 getEngineCbSize() const { return _engineCbSize; }
        /** @brief 엔진 CB 를 채울 멤버 표. 드로우마다 이름을 다시 해시하지 않기 위한 것. */
        const vector<ShaderEngineCbMember>& getEngineCbMembers() const { return _listEngineCbMember; }
        /** @brief 텍스처/구조버퍼 슬롯 바인딩 표. */
        const vector<ShaderResourceBind>& getResourceBinds() const { return _listResourceBind; }

    private:
        void rebuildIndex();
        void computeFingerprint();
        /** @brief 드로우마다 반복하던 계산을 여기서 한 번만 한다 (build 끝에서 호출). */
        void buildBindPlan();

        vector<ShaderBindingSlot>            _listSlot;
        unordered_map<hashed_string, uint32> _mapNameToSlot;
        uint64                               _fingerprint{ 0 };

        uint32                       _engineCbSize{ 0 };
        vector<ShaderEngineCbMember> _listEngineCbMember;
        vector<ShaderResourceBind>   _listResourceBind;
    };
} // namespace sw
