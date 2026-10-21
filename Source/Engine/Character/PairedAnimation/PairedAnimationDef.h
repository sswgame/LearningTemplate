/**
 * @file PairedAnimationDef.h
 * @brief 페어드 애니메이션 에셋(`*.pairedanim.xml`)입니다. 여러 개체가 한 타임라인으로 도는 애니메이션(활 쏘기, 잡기, 문 열기)의 역할 목록,
 *        역할마다 클립 · 시작 시각 · 정렬, 공유 타임라인 길이와 알림을 담습니다. 설계는 `docs/plans/PairedAnimation.md`.
 * @details 캐릭터 데이터 XML 관례(`CharacterDataReader`)로 읽는다. 모르는 속성 · 원소, 숫자가 아닌 값, 유한하지 않은 수(`nan` · `inf`),
 *          빠진 필수 값, 규칙 위반(앵커가 하나가 아님, 정렬 방식에 맞지 않는 속성, 타임라인 밖 알림)은 모두 읽기 오류다 — 조용히 기본값이 되지 않는다.
 *          @code
 *          <PairedAnimation duration="1.6">
 *            <Role name="Archer" anchor="true" required="true" clip="Bow_Shoot_Archer" enterBlend="0.15" exitState="Idle" exitBlend="0.2"/>
 *            <Role name="Bow" required="true" clip="Bow_Shoot_Bow" align="none" enterBlend="0" exitState="Bow_Idle" exitBlend="0.1"/>
 *            <Role name="Victim" required="true" clip="Grab_Victim" align="warp" offset="0 0 1" yaw="180" warpEnd="0.3" maxAlignDistance="1.5"
 *                  enterBlend="0.1" exitState="Idle" exitBlend="0.2"/>
 *            <Notify name="ReleaseArrow" time="0.9" role="Bow"/>
 *            <Select role="Victim" minAngle="-45" maxAngle="45" maxDistance="2"/>
 *          </PairedAnimation>
 *          @endcode
 *          `<Select>`(없거나 하나)는 세트 고르기 조건이다 — 그 역할이 앵커 앞에서 본 방위각(도) 구간과 수평 거리 안에 있어야 이 세트를 고른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    class CharacterDataReader;
    class XMLNode;

    /**
     * @enum PairedAnimationAlign
     * @brief 역할 루트를 앵커 기준 자리에 맞추는 방법입니다.
     */
    enum class PairedAnimationAlign : uint8
    {
        None = 0, ///< 맞추지 않는다(앵커, 소켓에 붙은 프롭).
        Snap,     ///< 진입 때 한 번 자리 · 요를 맞춘다.
        Warp,     ///< `MotionWarpingComponent` 창이 `warpEnd` 까지 루트 모션을 휘어 맞춘다. 목표는 매 프레임 앵커에서 다시 구한다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PairedAnimationRoleDef
     * @brief 역할 하나의 저작 데이터입니다. 정렬 오프셋은 정렬 시각(`Snap` 은 진입, `Warp` 는 `_warpEndSeconds`)의 앵커 루트 기준 이 역할 루트의 자리 · 요입니다.
     */
    struct PairedAnimationRoleDef
    {
        hashed_string        _name{};
        hashed_string        _clip{};
        hashed_string        _exitState{};
        string               _skeletonPath{};       ///< 비었으면 스켈레톤을 묻지 않는다
        float3               _offset{};             ///< 앵커 요로 돌릴 앵커 기준 자리(m)
        float32              _yawRadians{ 0.0f };   ///< 앵커 요에 더할 요(라디안)
        float32              _startSeconds{ 0.0f }; ///< 공유 타임라인에서 들어오는 시각
        float32              _enterBlendSeconds{ 0.0f };
        float32              _exitBlendSeconds{ 0.0f };
        float32              _warpEndSeconds{ 0.0f };   ///< `Warp` 창 끝(역할 클립 시각)
        float32              _maxAlignDistance{ 0.0f }; ///< 진입 조건 — 시작 때 루트와 목표의 거리 상한(m)
        PairedAnimationAlign _align{ PairedAnimationAlign::None };
        bool                 _bRequired{ false };
        bool                 _bAnchor{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 공유 타임라인 알림 하나입니다. 그 역할 오브젝트의 알림 표(`AnimationNotifyComponent`)로 순간 알림을 보냅니다. */
    struct PairedAnimationNotifyDef
    {
        hashed_string _name{};
        hashed_string _role{};
        float32       _time{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 세트 고르기 조건입니다(`<Select>`). 여러 에셋 가운데 시작할 것을 고를 때(처형 · 잡기 세트), 그 역할 루트가 앵커 루트에서 보이는 자리가 맞아야 합니다.
     * @details 방위각은 앵커 앞(+Z)에서 잰 수평 각(라디안, `atan2( x, z )`, 오른쪽 +X 가 양수)이고 거리는 수평 거리입니다. `_minBearingRadians > _maxBearingRadians` 면
     *          ±180° 를 지나는 구간(뒤쪽)입니다.
     */
    struct PairedAnimationSelectDef
    {
        hashed_string _role{};
        float32       _minBearingRadians{ 0.0f };
        float32       _maxBearingRadians{ 0.0f };
        float32       _maxDistance{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PairedAnimationContentDef
     * @brief 파일 한 장의 내용 그대로입니다(타임라인 길이 · 역할 · 알림 · 고르기 조건). 에디터는 이것을 고치고 `PairedAnimationDef::makeXMLText` 로 쓴 뒤 다시 읽어 검사합니다.
     * @details 각(`_yawRadians`, 고르기 방위각)은 라디안이고 파일에는 도로 적힌다. 알림은 읽은 뒤 시각 순서다.
     */
    struct PairedAnimationContentDef
    {
        vector<PairedAnimationRoleDef>   _listRole{};
        vector<PairedAnimationNotifyDef> _listNotify{};
        PairedAnimationSelectDef         _select{};
        float32                          _duration{ 0.0f };
        bool                             _bHasSelect{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PairedAnimationDef
     * @brief 페어드 애니메이션 에셋 하나입니다. 파일 머리말 참고.
     */
    class SW_API PairedAnimationDef
    {
    public:
        static constexpr string_view kExtension = ".pairedanim.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `PairedAnimationDef.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "PairedAnimationDef", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0 };

        PairedAnimationDef();

        /** @brief XML 텍스트에서 읽습니다(지금 내용을 비우고). 오류가 있으면 모두 로그로 내고 false 입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /**
         * @brief 내용을 파일 형식(`*.pairedanim.xml`)으로 씁니다. 적지 않아도 되는 값(앵커가 아닌 역할의 `anchor`, 0 인 `start` · `yaw`, 빈 `skeleton`)은 쓰지 않습니다.
         * @details 쓰기는 검사하지 않는다 — 결과를 `loadFromXMLText` 로 다시 읽어야 규칙(앵커 하나, 정렬 속성 등)을 본다. 정렬하지 않는 역할의 오프셋 · 워프 값은 쓰지 않는다.
         */
        [[nodiscard]] static string makeXMLText( const PairedAnimationContentDef& content );
        /** @brief 지금 내용을 파일 형식으로 씁니다(`makeXMLText`). */
        string saveToXMLText() const { return makeXMLText( _content ); }
        /** @brief 파일 한 장의 내용입니다. */
        const PairedAnimationContentDef& getContent() const { return _content; }

        /**
         * @brief 읽은 리소스 경로입니다(`loadFromResource`). 텍스트에서 읽었거나 코드가 만든 에셋은 비어 있습니다.
         * @details 네트워크 복제(`GF_NetPairedAnimation`)는 이 경로로 클라이언트가 같은 에셋을 받게 한다 — 경로가 빈 에셋의 세션은 복제할 수 없다(오류).
         */
        const string& getSourcePath() const { return _sourcePath; }
        /** @brief 공유 타임라인 길이(초)입니다. 앵커 클립 길이와 같아야 세션이 시작합니다. */
        float32 getDuration() const { return _content._duration; }
        /** @brief 역할들입니다(파일 순서). */
        const vector<PairedAnimationRoleDef>& getRoles() const { return _content._listRole; }
        /** @brief 공유 알림들입니다(시각 순서). */
        const vector<PairedAnimationNotifyDef>& getNotifies() const { return _content._listNotify; }
        /** @brief 앵커 역할의 인덱스입니다(읽기에 성공했으면 늘 있습니다). */
        uint32 getAnchorRoleIndex() const { return _anchorRoleIndex; }
        /** @brief 이름의 역할 인덱스입니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findRoleIndex( const hashed_string& roleName ) const;
        /** @brief 세트 고르기 조건입니다. 적지 않았으면(어느 자리에서나 고를 수 있는 세트) nullptr 입니다. */
        const PairedAnimationSelectDef* findSelect() const { return _content._bHasSelect ? &_content._select : nullptr; }
        /**
         * @brief 고르기 조건의 역할 루트가 @p rolePosition 이고 앵커 루트가 @p anchorWorld 일 때 이 세트를 고를 수 있으면 true 입니다. 조건이 없으면 늘 true 입니다.
         */
        bool matchesSelect( const float4x4& anchorWorld, const float3& rolePosition ) const;
        /**
         * @brief 앵커 루트가 @p anchorPosition · @p anchorYaw(라디안)일 때 역할 루트의 정렬 목표(자리 · 요)입니다. 오프셋은 앵커 요로 돌린다(+Z 가 앞).
         * @details 세션의 정렬(`snap` · `warp`)과 에디터 미리보기의 오프셋 기즈모가 같은 식을 쓴다. 같은 축 · 각 규칙을 `MotionWarpingComponent` 가 목표 요로 쓴다.
         */
        static void computeAlignTarget( const float3& anchorPosition, float32 anchorYaw, const PairedAnimationRoleDef& role, float3& outPosition, float32& outYaw );

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );
        void readRole( const XMLNode& node, CharacterDataReader& reader );
        void readNotify( const XMLNode& node, CharacterDataReader& reader );
        void readSelect( const XMLNode& node, CharacterDataReader& reader );
        /** @brief 역할 사이 · 알림의 규칙을 검사합니다(앵커 하나, 겹친 이름, 알림 시각과 역할). */
        void validate( CharacterDataReader& reader );
        /** @brief 내용을 비웁니다. */
        void clear();

        PairedAnimationContentDef _content;
        string                    _sourcePath;
        uint32                    _anchorRoleIndex;
    };
} // namespace sw
