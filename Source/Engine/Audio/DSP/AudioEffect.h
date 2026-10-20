/**
 * @file AudioEffect.h
 * @brief 버스 이펙트의 공통 틀입니다. 이름을 붙인 파라미터와 처리 함수, 그리고 데이터의 이름으로 이펙트를 만드는 레지스트리를 담습니다.
 * @details 이펙트 종류는 코드의 테이블(`AudioEffectRegistry`)에 항목 하나씩 있고, 데이터(`*.audiomixer.xml` 의 `AudioEffectDesc`)는 종류 이름과 파라미터 이름으로만
 *          고른다. 모르는 종류나 모르는 파라미터는 읽기 오류다. 파라미터는 스냅샷이 이름으로 바꾼다(`AudioSnapshotDesc`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioBlock.h"
#include "Engine/Audio/AudioChannelLayout.h"

namespace sw
{
    /** @brief 이펙트 파라미터 하나의 서술입니다. */
    struct AudioEffectParameterInfo
    {
        const utf8* _pName{ nullptr }; ///< 데이터와 스냅샷이 쓰는 이름이다.
        float32     _defaultValue{ 0.0f };
        float32     _minValue{ 0.0f };
        float32     _maxValue{ 1.0f };
        bool        _bLogarithmic{ false }; ///< 스냅샷 블렌드를 로그 축에서 한다(주파수)
    };
} // namespace sw

namespace sw
{
    class IAudioEffect;

    /** @brief 이펙트 종류 하나입니다(레지스트리의 한 줄). */
    struct AudioEffectTypeInfo
    {
        const utf8*                     _pName{ nullptr };
        const AudioEffectParameterInfo* _pParameter{ nullptr };
        uint32                          _parameterCount{ 0 };
        unique_ptr<IAudioEffect> ( *_pCreate )( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout ){ nullptr };
        uint8 _supportedLayoutMask{ 0 }; ///< 처리할 수 있는 레이아웃의 비트(`1 << AudioChannelLayout`)다. 믹서가 맞지 않는 그래프를 초기화 오류로 멈춘다.

        /** @brief 이 종류가 @p layout 을 처리할 수 있는지입니다. */
        bool supportsLayout( AudioChannelLayout layout ) const { return ( _supportedLayoutMask & makeLayoutBit( layout ) ) != 0; }
        /** @brief 레이아웃 하나의 비트입니다(`_supportedLayoutMask` 를 채울 때 씁니다). */
        static constexpr uint8 makeLayoutBit( AudioChannelLayout layout ) { return static_cast<uint8>( 1u << static_cast<uint32>( layout ) ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioEffect
     * @brief 평면 블록을 제자리에서 바꾸는 이펙트입니다. 파라미터는 종류가 서술한 순서의 float 배열입니다.
     * @details 만들 때 받은 레이아웃의 채널 수만큼 처리합니다(상태도 그만큼 잡습니다). 종류마다 지원 레이아웃을 표에 선언하고, 맞지 않으면 믹서가 초기화 오류로 멈춥니다.
     *          채널을 섞는 이펙트(리버브)는 LFE 를 입력에 넣지 않고 젖은 소리도 내지 않습니다. 컴프레서 · 리미터는 모든 채널을 한 검출기로 묶습니다(링크).
     * @details 파라미터가 바뀌면 `onParameterChanged` 가 계수를 다시 셉니다(오디오 스레드, 블록 경계).
     */
    class SW_API IAudioEffect
    {
    public:
        IAudioEffect( const AudioEffectTypeInfo& typeInfo, AudioChannelLayout layout );
        virtual ~IAudioEffect() = default;

        IAudioEffect( const IAudioEffect& )            = delete;
        IAudioEffect& operator=( const IAudioEffect& ) = delete;

        /** @brief @p block(평면, 만들 때의 채널 수, `_frameCount` 프레임)을 제자리에서 처리합니다. */
        virtual void process( const AudioBlockView& block ) = 0;
        /** @brief 지연선과 필터 상태를 비웁니다. */
        virtual void reset() = 0;

        /** @brief 종류입니다. */
        const AudioEffectTypeInfo& getTypeInfo() const { return *_pTypeInfo; }
        /** @brief 이름의 파라미터 번호입니다. 없으면 -1 입니다. */
        int32 findParameterIndex( const hashed_string& name ) const;
        /** @brief 파라미터를 정합니다(서술의 범위로 묶습니다). 바뀌었으면 계수를 다시 셉니다. */
        void setParameter( uint32 parameterIndex, float32 value );
        /** @brief 파라미터 값입니다. */
        float32 getParameter( uint32 parameterIndex ) const { return _listParameter[parameterIndex]; }
        /** @brief 만들 때 받은 레이아웃입니다. */
        AudioChannelLayout getChannelLayout() const { return _layout; }

    protected:
        /** @brief 파라미터가 바뀐 뒤 불립니다(만들 때도 한 번). */
        virtual void onParameterChanged() {}

    protected:
        const AudioEffectTypeInfo* _pTypeInfo;     /**< 종류입니다. */
        vector<float32>            _listParameter; /**< 파라미터 값(서술 순서)입니다. */
        uint32                     _channelCount;  /**< 처리할 채널 수입니다(레이아웃의 채널 수). */
        AudioChannelLayout         _layout;        /**< 처리할 레이아웃입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioEffectRegistry
     * @brief 이름에서 이펙트 종류로 가는 테이블입니다. LowPass, HighPass, BandPass, Peaking, LowShelf, HighShelf, Compressor, Limiter, Reverb, Delay 를 지원합니다.
     */
    struct SW_API AudioEffectRegistry
    {
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        static const AudioEffectTypeInfo* findType( const hashed_string& name );
        /** @brief 이름의 이펙트를 @p layout 용으로 기본 파라미터로 만듭니다. 모르는 이름이거나 종류가 그 레이아웃을 지원하지 않으면 nullptr 입니다(오류를 남깁니다). */
        static unique_ptr<IAudioEffect> createEffect( const hashed_string& name, AudioChannelLayout layout );
        /** @brief 번호의 종류입니다. */
        static const AudioEffectTypeInfo& getType( uint32 typeIndex );
    };
} // namespace sw
