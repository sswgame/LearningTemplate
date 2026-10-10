/**
 * @file AnimClip.h
 * @brief 스켈레탈 애니메이션 클립 에셋(`.animclip`) — 본 트랙(코덱 블롭) · 실수 커브 · 알림 · 루트 모션 트랙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimPlayback.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    class Skeleton;

    /**
     * @struct AnimCurveKey
     * @brief 커브 키 하나(시각 · 값)입니다.
     */
    struct AnimCurveKey
    {
        float32 _time{ 0.0f };
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimCurve
     * @brief 이름 붙은 실수 커브입니다(머티리얼 값 · 발 고정 가중치 · 속도 같은 게임 값). 키 사이는 선형, 바깥은 끝 키 값입니다.
     */
    struct SW_API AnimCurve
    {
        hashed_string        _name;
        vector<AnimCurveKey> _listKey; ///< 시각 오름차순입니다.

        /** @brief @p time 의 값입니다. 키가 없으면 0 입니다. */
        float32 evaluate( float32 time ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimClip
     * @brief 클립 하나입니다. 트랙(본 이름 순서)은 코덱 블롭에 들어 있고, 샘플은 코덱이 트랙 순서의 포즈로 풀어 냅니다.
     * @details 파일 형식(리틀 엔디언): 매직 `SWAC` · 버전 · 플래그(비트 0 = 반복) · 길이(초) · 이름 · 트랙 수와 트랙(본) 이름들 ·
     *          루트 모션 트랙(-1 = 없음) · 코덱 번호 · 블롭 크기와 블롭 · 알림(이름 · 시각 · 구간) · 커브(이름 · 키). 문자열은 길이(uint32) + 바이트입니다.
     *          읽기는 지금 형식만 받습니다(버전이 다르면 원본에서 다시 임포트합니다).
     */
    class SW_API AnimClip final : public IAnimPlayable
    {
    public:
        /** @brief 클립 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".animclip";
        /** @brief 지금 파일 형식 버전입니다. */
        static constexpr uint32 kVersion = 1;

        AnimClip();

        /** @brief (IAnimPlayable) 길이(초)입니다. */
        float32 getPlayLength() const override { return _duration; }
        /** @brief (IAnimPlayable) 데이터가 정한 반복 여부입니다. */
        bool isLoopingByDefault() const override { return _bLoop == SW_TRUE; }
        /** @brief (IAnimPlayable) 알림 트랙입니다. */
        const AnimNotifyTrack* findNotifyTrack() const override { return _notifyTrack.isEmpty() ? nullptr : &_notifyTrack; }

        const hashed_string&         getName() const { return _name; }
        float32                      getDuration() const { return _duration; }
        uint32                       getTrackCount() const { return static_cast<uint32>( _listTrackName.size() ); }
        const vector<hashed_string>& getTrackNames() const { return _listTrackName; }
        AnimCodecID                  getCodecID() const { return _codecID; }
        /** @brief 블롭 크기(바이트)입니다. */
        size_t getCodecByteCount() const { return _codecByteCount; }
        /** @brief 16 바이트 정렬된 블롭 시작입니다. */
        const uint8*             getCodecData() const { return reinterpret_cast<const uint8*>( _listCodecBlock.data() ); }
        const AnimNotifyTrack&   getNotifyTrack() const { return _notifyTrack; }
        const vector<AnimCurve>& getCurves() const { return _listCurve; }
        /** @brief 이름으로 커브를 찾습니다. 없으면 nullptr 입니다. */
        const AnimCurve* findCurve( const hashed_string& name ) const;
        /** @brief 루트 모션을 뽑을 트랙입니다. -1 이면 루트 모션이 없습니다. */
        int32 getRootMotionTrack() const { return _rootMotionTrack; }

        void setName( const hashed_string& name ) { _name = name; }
        void setLooping( bool bLoop ) { _bLoop = bLoop ? SW_TRUE : SW_FALSE; }
        void setRootMotionTrack( int32 trackIndex ) { _rootMotionTrack = trackIndex; }
        /** @brief 알림을 하나 더합니다(임포트 · 시험). */
        void addNotify( const AnimNotifyEvent& event ) { _notifyTrack.addEvent( event ); }
        /** @brief 커브를 하나 더합니다(임포트 · 시험). */
        void addCurve( const AnimCurve& curve ) { _listCurve.push_back( curve ); }

        /**
         * @brief 원본 표본을 @p codec 으로 압축해 이 클립의 트랙 · 길이 · 블롭으로 삼습니다(임포트 · 시험). 이름 · 알림 · 커브는 그대로입니다.
         * @param pOutStats 압축률 · 최대 오차를 받습니다(선택).
         */
        [[nodiscard]] bool compressFrom( const AnimRawClip& rawClip, const IAnimCodec& codec, const AnimCodecSettings& settings, AnimCodecStats* pOutStats );

        /**
         * @brief @p time 초의 트랙 포즈(트랙 순서)를 @p outTrackPose 에 씁니다. 코덱이 없거나 블롭이 깨졌으면 false 입니다.
         * @param pTrackMask 트랙마다 0 이면 풀지 않습니다(본 LOD). nullptr 이면 모든 트랙입니다.
         */
        [[nodiscard]] bool sampleTracks( float32 time, Pose& outTrackPose, const uint8* pTrackMask = nullptr ) const;
        /** @brief 트랙 → 스켈레톤 본 인덱스 표를 만듭니다(없는 본은 -1). 클립 · 스켈레톤 짝마다 한 번 만들어 둡니다. */
        void makeTrackToBoneMap( const Skeleton& skeleton, vector<int32>& outListBone ) const;
        /**
         * @brief 클립을 @p time 에 샘플해 본 포즈(레퍼런스 · 리더 포즈로 채워 둔 @p inoutPose)에 씁니다. 애니메이터 · 군중 공유 · VAT 굽기가 함께 쓰는 한 길입니다.
         * @param listTrackToBone   `makeTrackToBoneMap` 의 표입니다.
         * @param scratchTrackPose  트랙 순서 포즈를 담을 재사용 자리입니다.
         * @param bAnchorRootMotion 루트 모션 트랙의 본을 클립 시작 자리에 묶습니다(움직임은 오브젝트가 맡는다).
         * @param pTrackMask        트랙마다 0 이면 풀지도 옮기지도 않습니다(본 LOD).
         */
        [[nodiscard]] bool samplePose( float32 time, const vector<int32>& listTrackToBone, Pose& inoutPose, Pose& scratchTrackPose, bool bAnchorRootMotion,
                                       const uint8* pTrackMask = nullptr ) const;
        /** @brief 트랙 포즈를 표대로 본 포즈에 옮깁니다. 표에 없는 본과 @p pTrackMask 가 0 인 트랙은 그대로 둡니다. */
        static void copyTracksToPose( const Pose& trackPose, const vector<int32>& listTrackToBone, Pose& inoutPose, const uint8* pTrackMask = nullptr );
        /**
         * @brief 루트 모션 트랙이 @p step 동안 움직인 양입니다(부모 공간 이동 차이 · 회전 차이 `inverse( 이전 ) * 지금`).
         * @details 반복을 넘으면 (끝 - 이전) + 온 바퀴 × (끝 - 시작) + (지금 - 시작) 입니다. 루트 모션 트랙이 없으면 단위 변환입니다.
         */
        BoneTransform computeRootMotionDelta( const AnimTimeStep& step ) const;
        /** @brief 루트 모션 트랙의 시각 0 변환입니다(뽑아낸 뒤 루트를 이 자리에 묶습니다). */
        BoneTransform getRootMotionAnchor() const;

        /** @brief 파일 바이트로 만듭니다. */
        void makeBytes( vector<uint8>& outBytes ) const;
        /** @brief 파일 바이트를 읽습니다. 형식이 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel );
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );

    private:
        /** @brief 블롭을 정렬된 저장소에 옮깁니다. */
        void assignCodecBytes( const uint8* pData, size_t byteCount );
        /** @brief 루트 모션 트랙 하나를 @p time 에서 샘플합니다. */
        BoneTransform sampleRootMotionTrack( float32 time ) const;
        /** @brief 내용을 비웁니다. */
        void clear();

        hashed_string          _name;
        vector<hashed_string>  _listTrackName;
        vector<AnimCodecBlock> _listCodecBlock;
        size_t                 _codecByteCount;
        AnimNotifyTrack        _notifyTrack;
        vector<AnimCurve>      _listCurve;
        float32                _duration;
        int32                  _rootMotionTrack;
        AnimCodecID            _codecID;
        uint8                  _bLoop;
    };
} // namespace sw
