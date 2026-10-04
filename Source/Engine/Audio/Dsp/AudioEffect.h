/**
 * @file AudioEffect.h
 * @brief 버스 이펙트의 공통 틀 — 이름 붙인 파라미터 · 처리 함수와, 데이터의 이름으로 이펙트를 만드는 등록부입니다.
 * @details 이펙트 종류는 코드의 표(`AudioEffectRegistry`) 한 줄씩이고, 데이터(`*.audiomixer.xml` 의 `AudioEffectDesc`)는 종류 이름과 파라미터 이름으로만
 *          고릅니다. 모르는 종류 · 모르는 파라미터는 읽기 오류입니다. 파라미터는 스냅샷이 이름으로 바꿉니다(`AudioSnapshotDesc`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 이펙트 파라미터 하나의 서술입니다. */
    struct AudioEffectParameterInfo
    {
        const utf8* _pName{ nullptr }; ///< 데이터 · 스냅샷이 쓰는 이름
        float32     _defaultValue{ 0.0f };
        float32     _minValue{ 0.0f };
        float32     _maxValue{ 1.0f };
        bool        _bLogarithmic{ false }; ///< 스냅샷 블렌드를 로그 축에서 한다(주파수)
    };
} // namespace sw

namespace sw
{
    class IAudioEffect;

    /** @brief 이펙트 종류 하나입니다(등록부의 한 줄). */
    struct AudioEffectTypeInfo
    {
        const utf8*                     _pName{ nullptr };
        const AudioEffectParameterInfo* _pParameter{ nullptr };
        uint32                          _parameterCount{ 0 };
        unique_ptr<IAudioEffect> ( *_pCreate )( const AudioEffectTypeInfo& typeInfo ){ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioEffect
     * @brief 스테레오 교차 버퍼를 제자리에서 바꾸는 이펙트입니다. 파라미터는 종류가 서술한 순서의 float 배열입니다.
     * @details 파라미터가 바뀌면 `onParameterChanged` 가 계수를 다시 셉니다(오디오 스레드, 블록 경계).
     */
    class SW_API IAudioEffect
    {
    public:
        explicit IAudioEffect( const AudioEffectTypeInfo& typeInfo );
        virtual ~IAudioEffect() = default;

        IAudioEffect( const IAudioEffect& )            = delete;
        IAudioEffect& operator=( const IAudioEffect& ) = delete;

        /** @brief @p frameCount 프레임(스테레오 교차)을 제자리에서 처리합니다. */
        virtual void process( float32* pInterleaved, uint32 frameCount ) = 0;
        /** @brief 지연선 · 필터 상태를 비웁니다. */
        virtual void reset() = 0;

        /** @brief 종류입니다. */
        const AudioEffectTypeInfo& getTypeInfo() const { return *_pTypeInfo; }
        /** @brief 이름의 파라미터 번호입니다. 없으면 -1 입니다. */
        int32 findParameterIndex( const hashed_string& name ) const;
        /** @brief 파라미터를 정합니다(서술의 범위로 묶습니다). 바뀌었으면 계수를 다시 셉니다. */
        void setParameter( uint32 parameterIndex, float32 value );
        /** @brief 파라미터 값입니다. */
        float32 getParameter( uint32 parameterIndex ) const { return _listParameter[parameterIndex]; }

    protected:
        /** @brief 파라미터가 바뀐 뒤 불립니다(만들 때도 한 번). */
        virtual void onParameterChanged() {}

    protected:
        const AudioEffectTypeInfo* _pTypeInfo;     /**< 종류입니다. */
        vector<float32>            _listParameter; /**< 파라미터 값(서술 순서)입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioEffectRegistry
     * @brief 이름 → 이펙트 종류 표입니다. LowPass · HighPass · BandPass · Peaking · LowShelf · HighShelf · Compressor · Limiter · Reverb · Delay.
     */
    struct SW_API AudioEffectRegistry
    {
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        static const AudioEffectTypeInfo* findType( const hashed_string& name );
        /** @brief 이름의 이펙트를 기본 파라미터로 만듭니다. 모르는 이름이면 nullptr 입니다. */
        static unique_ptr<IAudioEffect> createEffect( const hashed_string& name );
        /** @brief 등록된 종류 수입니다. */
        static uint32 getTypeCount();
        /** @brief 번호의 종류입니다. */
        static const AudioEffectTypeInfo& getType( uint32 typeIndex );
    };
} // namespace sw
