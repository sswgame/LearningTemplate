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

    private:
        void rebuildIndex();
        void computeFingerprint();

        vector<ShaderBindingSlot>            _listSlot;
        unordered_map<hashed_string, uint32> _mapNameToSlot;
        uint64                               _fingerprint{ 0 };
    };
} // namespace sw
