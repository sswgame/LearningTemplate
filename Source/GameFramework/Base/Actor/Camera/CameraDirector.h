/**
 * @file CameraDirector.h
 * @brief 매 프레임의 카메라 포즈를 내는 순수 상태 기계를 정의합니다.
 * @details 활성 프리셋, 모드 상태, 블렌드 스택, 감쇠, 흔들림을 시간으로 굴린다. 컴포넌트와 씬은 모른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraMode.h"
#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Actor/Camera/CameraShake.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ICameraCollisionProbe;

    /**
     * @class CameraDirector
     * @brief 프리셋을 켜고(`activatePreset`) 입력을 넣고(`applyInput`) 시간을 흘리면(`step`), 모드, 감쇠, 블렌드, 흔들림을 거친 포즈를 줍니다.
     * @details 켠 프리셋은 **블렌드 스택**에 쌓인다. Cinemachine 의 중첩 블렌드와 같은 방식이다.
     *          스택의 맨 위가 지금 프리셋이고, 아래 층은 위 층이 다 섞일 때까지 대상을 계속 따라간다(살아 있는 쪽, 자기 모드 상태로).
     *          블렌드 도중에 다시 켜면 진행 중인 블렌드가 멈추지 않고 그대로 섞이며, 그 섞인 결과에서 새 프리셋으로 섞어 간다.
     *          그래서 켠 순간 화면이 튀지 않고, 나가던 블렌드의 움직임도 살아 있다.
     *          층이 `kMaxLayerCount` 를 넘으면 가장 아래 둘을 그 순간의 섞인 포즈로 고정해 하나로 합친다.
     *          층이 다 섞이면(가중치 1) 그 아래 층은 버린다.
     *          감쇠와 손떨림은 층마다의 포즈에 걸린다. 새로 켠 프리셋은 감쇠 없이 그 자리에서 시작한다.
     *          충격(`addImpulse`)은 섞인 포즈 위에 마지막으로 얹는다.
     *          처음 켠 프리셋(아직 낸 포즈가 없다)과 컷 블렌드는 스택을 비우고 바로 붙는다.
     *          포즈를 내던 중의 컷은 `consumeCut` 이 한 번 알리며, 렌더러의 컷 신호로 쓴다.
     *          스택(프리셋 id, 블렌드 진행, 입력이 돌린 상태)은 `writeState` / `readState` 로 저장한다.
     */
    class SW_GF_API CameraDirector
    {
    public:
        /** @brief 블렌드 스택의 최대 층 수입니다. 넘치면 가장 아래 둘을 고정 포즈 하나로 합칩니다. */
        static constexpr uint32 kMaxLayerCount = 4;

        CameraDirector();

        /** @brief 카탈로그의 프리셋을 켭니다. 블렌드는 `catalog.getBlend( 지금 프리셋, id )` 로 구합니다. 없는 id 면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id );
        /** @brief 카탈로그의 프리셋을 @p blend 로 켭니다. 블렌드 표는 무시합니다. */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id, const BlendCurveDef& blend );
        /** @brief 정의를 그대로 켭니다. 카탈로그 밖의 프리셋(시퀀서나 코드로 만든 것)에 쓰며, 모드 상태는 새로 시작합니다. */
        void activatePreset( const CameraPresetDef& def, const BlendCurveDef& blend );
        /** @brief 켠 프리셋의 값만 바꿉니다. 블렌드와 모드 상태는 그대로 두며, 리그가 매 프레임 바뀌는 값(덮어쓴 탑승 시점)을 넣을 때 씁니다. */
        void refreshActivePreset( const CameraPresetDef& def );
        /**
         * @brief 화면이 지금 @p from 이라고 보고, 거기서 켠 프리셋으로 @p blend 를 따라 섞습니다. 스택의 다른 층은 버립니다.
         * @details 다른 카메라가 그리던 화면(시네마틱 컷의 마지막 포즈, 게임 카메라)에서 이 디렉터로 넘어올 때 쓴다. 언리얼의 시퀀스 블렌드 아웃과 같다.
         *          켠 프리셋이 없으면 아무것도 하지 않는다. 컷 블렌드면 그대로 붙는다.
         */
        void blendFromPose( const CameraPose& from, const BlendCurveDef& blend );

        /** @brief 이번 프레임의 입력을 켠 프리셋의 모드 상태에 넣습니다(`applyCameraInput`). */
        void applyInput( const CameraModeInput& input, float32 deltaTime );
        /** @brief 암 충돌 질의를 정합니다. 수명은 부르는 쪽이 쥐며, nullptr 이면 쓸어 보지 않습니다. */
        void setCollisionProbe( const ICameraCollisionProbe* pProbe ) { _pProbe = pProbe; }
        /** @brief 충격을 더합니다. 원점과 모양을 받으며, 듣는 자리는 카메라입니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin ) { _impulseListener.addImpulse( def, origin ); }
        /** @brief 시야각 킥(+) · 줌 펀치(−)를 더합니다. 섞인 포즈의 시야각과 직교 높이에 마지막으로 곱합니다. */
        void addFieldOfViewKick( const CameraFieldOfViewKickDef& def ) { _fieldOfViewKickListener.addKick( def ); }

        /** @brief 시간을 흘리고 이번 프레임의 포즈를 반환합니다. 켠 프리셋이 없으면 지난 포즈를 그대로 반환하며, 처음에는 기본값입니다. */
        const CameraPose& step( float32 deltaTime, const CameraTarget& target );

        /**
         * @brief 스택(층마다 프리셋 id, 블렌드, 진행, 감쇠한 포즈, 입력이 돌린 상태)을 씁니다. 첫 값은 형식 표와 버전입니다.
         * @details 프리셋 본문은 쓰지 않는다. 읽을 때 카탈로그에서 다시 찾고, 카탈로그에 없는 층(코드로 만든 프리셋)은 마지막 포즈로 고정해 둔다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 스택을 복원합니다. 형식이 맞지 않거나 잘렸으면 false 이고 지금 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive, const CameraPresetCatalog& catalog );

        const hashed_string&   getActivePresetID() const { return getActivePreset()._id; }
        const CameraPresetDef& getActivePreset() const;
        bool                   hasActivePreset() const { return _listLayer.empty() == false && _listLayer.back()._bFrozen == SW_FALSE; }
        /** @brief 아래 층으로부터 섞이는 중인 층이 있으면 true 입니다. */
        bool isBlending() const { return _listLayer.size() > 1; }
        /** @brief 블렌드 스택의 층 수입니다. 섞는 중이 아니면 1 이고, 켠 프리셋이 없으면 0 입니다. */
        uint32 getLayerCount() const { return static_cast<uint32>( _listLayer.size() ); }
        /** @brief 맨 위 층(지금 프리셋)의 블렌드 가중치입니다. 0 이 아래 층들이고 1 이 지금 프리셋이며, 블렌드 중이 아니면 1 입니다. */
        float32           getBlendWeight() const;
        const CameraPose& getPose() const { return _outputPose; }
        bool              hasPose() const { return _bHasPose == SW_TRUE; }
        /** @brief 켠 프리셋의 모드 상태입니다. 입력이 돌린 각, 줌, 팬, 암 길이를 담습니다. 켠 프리셋이 없으면 빈 상태입니다. */
        CameraModeState&       getModeState();
        const CameraModeState& getModeState() const;
        /** @brief 포즈를 내던 중 컷으로 바꿨으면 true 입니다. 읽으면 지우므로, 카메라에 컷 표시를 넣는 쪽이 한 번만 읽습니다. */
        bool consumeCut();

    private:
        /** @brief 블렌드 스택의 층 하나입니다. 고정 층은 프리셋을 풀지 않고 `_pose` 를 그대로 냅니다. */
        struct Layer
        {
            CameraPresetDef _def{};
            CameraModeState _state{};
            BlendCurveDef   _blend{};      ///< 아래 층에서 이 층으로 섞는 곡선
            CameraPose      _dampedPose{}; ///< 감쇠를 거친 포즈(손떨림 전)
            CameraPose      _pose{};       ///< 이번 프레임에 이 층이 낸 포즈(손떨림 후). 고정 층은 이것이 출발점이다
            float32         _elapsed{ 0.0f };
            float32         _weight{ 1.0f };
            uint8           _bFrozen{ SW_FALSE }; ///< 프리셋 없이 고정한 포즈
            uint8           _bFresh{ SW_FALSE };  ///< 다음 `step` 이 감쇠 없이 그 자리에서 시작한다
        };

        /** @brief 층 하나를 이번 프레임 포즈로 풉니다. 모드, 감쇠(새로 켰으면 그 자리), 손떨림 순으로 처리합니다. 고정 층은 그대로입니다. */
        void evaluateLayer( Layer& inoutLayer, float32 deltaTime, const CameraTarget& target ) const;
        /** @brief 층이 넘치면 가장 아래 둘을 그 둘의 섞인 포즈로 고정해 하나로 합칩니다. */
        void collapseOverflow();
        /** @brief 고정 층 하나를 만듭니다. */
        static Layer makeFrozenLayer( const CameraPose& pose );

    private:
        vector<Layer>                 _listLayer; ///< [0] 이 가장 아래(가장 오래된) 층, 끝이 지금 프리셋
        CameraImpulseListener         _impulseListener;
        CameraFieldOfViewKickListener _fieldOfViewKickListener;
        CameraModeState               _emptyState; ///< 켠 프리셋이 없을 때 `getModeState` 가 내주는 빈 상태
        CameraPose                    _outputPose;
        const ICameraCollisionProbe*  _pProbe;
        uint8                         _bHasPose   : 1;
        uint8                         _bCutQueued : 1;
        [[maybe_unused]] uint8        _reserved   : 6;
    };
} // namespace sw
