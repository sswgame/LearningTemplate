/**
 * @file AnimCodec.h
 * @brief 애니메이션 코덱 인터페이스 — 쿠킹 · 임포트 때 압축(코덱 id + 불투명 블롭), 런타임에 샘플링. 코덱 등록부와 오차 측정입니다.
 * @details 라이브러리(ACL 등)는 코덱 백엔드 폴더(`Codec/<이름>/`) 안에서만 보입니다. 바깥은 이 헤더의 타입만 압니다 —
 *          언리얼 `UAnimBoneCompressionCodec` · Godot PhysicsServer 처럼 구현을 갈아 끼울 수 있게 하는 경계입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    /**
     * @enum AnimCodecId
     * @brief 클립 파일에 적히는 코덱 번호입니다. 값은 파일 형식의 일부라 바꾸지 않습니다.
     */
    enum class AnimCodecId : uint8
    {
        Raw = 0, ///< 압축하지 않은 균일 샘플(기준 · 디버그)
        Acl = 1, ///< Animation Compression Library 2.1(가변 비트율 · 상수 트랙 제거 · 오차 기준 키 줄이기)
        Count,
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimCodecBlock
     * @brief 코덱 블롭을 담는 16 바이트 단위입니다. 이것의 배열로 보관하면 블롭 시작이 16 바이트 정렬입니다(ACL 이 요구합니다).
     */
    struct alignas( 16 ) AnimCodecBlock
    {
        uint8 _arrByte[16];
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimRawClip
     * @brief 압축 전의 클립 — 트랙(본)마다 같은 간격으로 뽑은 변환입니다. 임포터가 원본 키를 이 모양으로 다시 뽑아 코덱에 넘깁니다.
     * @details 표본 s 의 트랙 t 는 `_listSample[s * trackCount + t]` 입니다. 길이는 `(sampleCount - 1) / sampleRate` 초입니다.
     *          `_listTrackParent` 는 오차 측정(모델 공간의 가상 정점)과 ACL 의 계층 오차 기준에 씁니다(루트 -1, 부모가 앞).
     */
    struct SW_API AnimRawClip
    {
        vector<hashed_string> _listTrackName;
        vector<int32>         _listTrackParent;
        vector<BoneTransform> _listSample;
        float32               _sampleRate{ 30.0f };
        uint32                _sampleCount{ 0 };

        uint32  getTrackCount() const { return static_cast<uint32>( _listTrackName.size() ); }
        float32 getDuration() const { return ( _sampleCount > 1 && _sampleRate > 0.0f ) ? static_cast<float32>( _sampleCount - 1 ) / _sampleRate : 0.0f; }
        /** @brief 표본 하나의 트랙 하나입니다. */
        const BoneTransform& getSample( uint32 sampleIndex, uint32 trackIndex ) const { return _listSample[sampleIndex * getTrackCount() + trackIndex]; }
        /** @brief 두 표본 사이를 보간해 @p outPose(트랙 수)에 씁니다 — 이동 · 스케일 선형, 회전 nlerp. 코덱의 기준값입니다. */
        void sample( float32 time, Pose& outPose ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimCodecSettings
     * @brief 압축 오차 기준입니다. 단위는 엔진 단위(미터)입니다. 임포트 규칙(데이터)이 정합니다.
     */
    struct AnimCodecSettings
    {
        float32 _precision{ 0.0001f };  ///< 가상 정점이 벗어나도 되는 거리(미터)입니다.
        float32 _shellDistance{ 0.1f }; ///< 가상 정점을 본에서 얼마나 떨어뜨려 재는지(미터)입니다 — 캐릭터 살갗 두께 정도.
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimCodec
     * @brief 코덱 하나입니다. 블롭은 그 코덱만 읽을 수 있는 불투명 바이트이고, 런타임 샘플링은 트랙 순서의 포즈를 냅니다.
     * @details 구현은 상태가 없습니다(여러 스레드가 함께 부릅니다).
     */
    class SW_API IAnimCodec
    {
    public:
        IAnimCodec()                               = default;
        virtual ~IAnimCodec()                      = default;
        IAnimCodec( const IAnimCodec& )            = delete;
        IAnimCodec& operator=( const IAnimCodec& ) = delete;

        /** @brief 코덱 번호입니다. */
        virtual AnimCodecId getId() const = 0;
        /** @brief 데이터(임포트 규칙)가 고르는 이름입니다("raw" · "acl"). */
        virtual const utf8* getName() const = 0;
        /** @brief 클립을 압축해 @p outBytes 에 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] virtual bool compress( const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes ) const = 0;
        /**
         * @brief 블롭을 @p time 초에서 샘플해 트랙 순서의 @p outPose 에 씁니다(본 수 = 트랙 수로 맞춥니다).
         * @param pBytes     16 바이트 정렬된 블롭입니다(`AnimClip` 이 그렇게 보관합니다).
         * @param pTrackMask 트랙마다 0 이면 그 트랙을 풀지 않습니다(본 LOD — 값은 단위 변환으로 남습니다). nullptr 이면 모든 트랙입니다.
         */
        [[nodiscard]] virtual bool sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask = nullptr ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimCodecStats
     * @brief 압축 한 번의 결과 요약입니다. 임포트가 클립마다 보고합니다.
     */
    struct AnimCodecStats
    {
        uint32  _rawByteCount{ 0 };        ///< 압축 전 크기(트랙 × 표본 × 10 float)입니다.
        uint32  _compressedByteCount{ 0 }; ///< 블롭 크기입니다.
        float32 _maxError{ 0.0f };         ///< 가상 정점의 최대 거리 오차(미터)입니다.

        /** @brief 압축률(원본 / 압축)입니다. */
        float32 computeRatio() const { return _compressedByteCount > 0 ? static_cast<float32>( _rawByteCount ) / static_cast<float32>( _compressedByteCount ) : 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimCodecRegistry
     * @brief 코덱 등록부입니다. 코드는 이름 붙은 코덱만 들고, 어느 것을 쓸지는 데이터(이름)가 고릅니다. 모르는 이름 · 번호는 nullptr 입니다.
     */
    struct SW_API AnimCodecRegistry
    {
        /** @brief 번호로 찾습니다. */
        static const IAnimCodec* findCodec( AnimCodecId id );
        /** @brief 이름으로 찾습니다(대소문자 무시). */
        static const IAnimCodec* findCodecByName( string_view name );
        /** @brief 압축하고 오차를 잽니다(임포트 · 시험). */
        [[nodiscard]] static bool compressAndMeasure( const IAnimCodec& codec, const AnimRawClip& rawClip, const AnimCodecSettings& settings,
                                                      vector<uint8>& outBytes, AnimCodecStats& outStats );
        /**
         * @brief 원본과 블롭을 같은 시각들(표본마다 + 표본 사이 가운데)에서 샘플해 모델 공간 가상 정점(본 원점 + 세 축 방향 shell 거리)의 최대 거리를 잽니다.
         * @details 코덱과 무관한 한 벌의 잣대라 Raw · ACL 을 같은 숫자로 비교합니다.
         */
        static float32 measureMaxError( const IAnimCodec& codec, const AnimRawClip& rawClip, const uint8* pBytes, size_t byteCount, float32 shellDistance );
    };
} // namespace sw
