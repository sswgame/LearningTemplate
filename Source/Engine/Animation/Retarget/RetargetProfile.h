/**
 * @file RetargetProfile.h
 * @brief 리타깃 프로필(`*.retarget.json`)입니다. 원본과 대상 스켈레톤 사이의 사슬 쌍(이름, 본 목록), 골반과 뿌리 처리, IK 목표 사슬을 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class JSONValue;

    /** @brief 뿌리와 골반 이동을 옮기는 방법입니다. */
    enum class RetargetTranslationMode : uint8
    {
        ScaleByPelvisHeight = 0, ///< 골반 높이 비(대상 / 원본)만큼 줄이거나 늘린다. 다리 길이가 다른 캐릭터가 같은 보폭 비로 걷는다.
        Copy,                    ///< 그대로 옮긴다
        None,                    ///< 대상 레퍼런스 위치에 둔다(제자리 동작).
    };

    /**
     * @brief 사슬 하나의 쌍입니다. 본 수가 달라도 됩니다. 대상 본마다 사슬 길이 비율로 원본 본을 고릅니다.
     * @details `_bIKGoal` 이면 회전을 옮긴 뒤 끝을 원본 끝 위치(골반 높이 비로 바꾼 값)로 IK 해서, 발이 미끄러지지 않고 디디게 한다.
     */
    struct RetargetChain
    {
        hashed_string         _name{};
        vector<hashed_string> _listSourceBone{};
        vector<hashed_string> _listTargetBone{};
        uint8                 _bIKGoal{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RetargetProfile
     * @brief 프로필 하나입니다. 형식은 JSON 이며, 모르는 키나 겹친 사슬 이름은 오류입니다.
     * @code
     * { "source_skeleton": "game/.../knight.skeleton.json", "target_skeleton": "game/.../skeleton_minion.skeleton.json",
     *   "root": { "source": "root", "target": "root" }, "pelvis": { "source": "hips", "target": "hips" },
     *   "translation": "ScaleByPelvisHeight",
     *   "chains": [ { "name": "LeftLeg", "source": ["upperleg.l", "lowerleg.l", "foot.l"], "target": ["upperleg.l", "lowerleg.l", "foot.l"], "ik_goal": true } ] }
     * @endcode
     *          사슬, 골반, 뿌리에 속하지 않은 대상 본은 대상 레퍼런스 포즈 그대로입니다(IK 보조 본, 손가락).
     */
    class SW_API RetargetProfile
    {
    public:
        /** @brief 프로필 확장자입니다. */
        static constexpr string_view kExtension = ".retarget.json";

        RetargetProfile();

        /** @brief JSON 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseJSON( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        void               clear();

        const string&                getSourceSkeletonPath() const { return _sourceSkeletonPath; }
        const string&                getTargetSkeletonPath() const { return _targetSkeletonPath; }
        const hashed_string&         getSourceRoot() const { return _sourceRoot; }
        const hashed_string&         getTargetRoot() const { return _targetRoot; }
        const hashed_string&         getSourcePelvis() const { return _sourcePelvis; }
        const hashed_string&         getTargetPelvis() const { return _targetPelvis; }
        RetargetTranslationMode      getTranslationMode() const { return _translationMode; }
        const vector<RetargetChain>& getChains() const { return _listChain; }
        /** @brief 사슬을 추가합니다(코드로 만드는 프로필과 테스트용). */
        void addChain( const RetargetChain& chain ) { _listChain.push_back( chain ); }
        /** @brief 뿌리와 골반 쌍, 이동 방법을 정합니다(코드로 만드는 프로필과 테스트용). */
        void setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis, const hashed_string& targetPelvis,
                               RetargetTranslationMode mode );

    private:
        [[nodiscard]] bool parseRoot( const JSONValue& root, string_view sourceLabel );

        vector<RetargetChain>   _listChain;
        string                  _sourceSkeletonPath;
        string                  _targetSkeletonPath;
        hashed_string           _sourceRoot;
        hashed_string           _targetRoot;
        hashed_string           _sourcePelvis;
        hashed_string           _targetPelvis;
        RetargetTranslationMode _translationMode;
    };
} // namespace sw
