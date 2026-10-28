/**
 * @file AnimationNotifyTable.h
 * @brief 애니메이션 알림 테이블(`*.notifies.xml`)입니다. 클립의 알림 이름에서 처리기(이름으로 고름)와 인자로 가는 매핑을 담습니다. 처리기 레지스트리와 처리기 인터페이스도 여기에 있습니다.
 * @details 클립은 알림의 **이름, 시각, 길이**만 가진다(임포트 옆 데이터 `<모델>.clips.json`). 그 이름이 무엇을 하는지(발소리, 이펙트, 칼 궤적 판정,
 *          카메라 흔들림, 게임플레이 이벤트, 모션 워핑 창)는 캐릭터마다 이 테이블의 한 항목이 정하고, 코드에는 처리기 **종류**만 있다(언리얼의 AnimationNotify 클래스,
 *          유니티의 Animation Event 함수 이름에 해당한다). 모르는 처리기나 인자, 숫자가 아닌 값, 겹친 알림 이름은 읽기 오류다.
 *          `<FootContact>` 는 그 알림을 클립 대신 발 닿음 감지(`AnimationFootContactDef`, 3D 만)가 내게 한다. 없는 알림 이름, 겹친 알림, 높이 순서가 틀린 값은 읽기 오류다.
 *          @code
 *          <AnimationNotifies>
 *            <Notify name="FootL" handler="Footstep" socket="foot.l" distance="0.4" sound="common/audio/footstep_{surface}.wav"/>
 *            <Notify name="Swing" handler="HitWindow" socketA="handslot.r" socketB="SwordTip" radius="0.05" damage="10" impulse="40"/>
 *            <Notify name="Impact" handler="CameraShake" amplitude="0.05" duration="0.25"/>
 *            <Notify name="Taunt" handler="GameplayEvent" event="Taunted"/>
 *            <FootContact notify="FootL" bone="foot.l" contact_height="0.05" release_height="0.12" max_vertical_speed="0.6"/>
 *          </AnimationNotifies>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/AnimationPlayback.h"
#include "Engine/Animation/Notify/AnimationFootContact.h"
#include "Engine/Asset/AssetID.h"

namespace sw
{
    class AnimationNotifyComponent;
    class AnimationNotifyHandlerRegistry;
    class CharacterDataReader;
    class GameObject;
    class IAnimationNotifyHandler;
    class XMLNode;

    /** @brief 처리기 인자의 종류입니다. 읽을 때 이 종류로 검사합니다(숫자 필드에 문자열이 있으면 오류입니다). */
    enum class AnimationNotifyParamKind : uint8
    {
        Name = 0, ///< 이름이다(소켓, 이벤트, 레이어). `hashed_string` 으로 보관한다.
        Text,     ///< 글이다(사운드나 프리팹 경로).
        Float,    ///< 실수다.
        Vector,   ///< 세 수 "x y z" 다.
        Bool,     ///< 참과 거짓이다.
    };

    /**
     * @brief 처리기가 받는 인자 하나의 선언입니다. 선택 인자의 "적지 않음" 값도 여기 한 곳에 둡니다.
     * @details 테이블을 읽을 때 적지 않은 선택 인자는 이 선언의 값으로 채운다 — 그래서 처리기는 인자를 읽을 때 대신할 값을 넘기지 않는다.
     *          이름 · 글 · 벡터의 "적지 않음" 은 빈 이름 · 빈 글 · 영벡터다.
     */
    struct AnimationNotifyParamDef
    {
        const utf8*              _pName{ nullptr };
        AnimationNotifyParamKind _kind{ AnimationNotifyParamKind::Name };
        bool                     _bRequired{ false };
        float32                  _absentNumber{ 0.0f }; ///< 적지 않은 선택 실수 인자의 값
        bool                     _bAbsentBool{ false }; ///< 적지 않은 선택 참거짓 인자의 값
    };
} // namespace sw

namespace sw
{
    /** @brief 테이블 항목 하나에 적힌 인자 값 하나입니다(종류에 맞는 필드만 씁니다). */
    struct AnimationNotifyParam
    {
        hashed_string            _name{};
        hashed_string            _nameValue{};
        string                   _text{};
        float3                   _vector{};
        float32                  _number{ 0.0f };
        AnimationNotifyParamKind _kind{ AnimationNotifyParamKind::Name };
        bool                     _bValue{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 테이블 항목 하나입니다. 알림 이름에서 처리기와 인자로 가는 매핑입니다. 처리기 포인터는 레지스트리(엔진 수명)가 소유합니다. */
    struct SW_API AnimationNotifyEntry
    {
        hashed_string                  _notify{};
        hashed_string                  _handlerName{};
        vector<AnimationNotifyParam>   _listParam{};
        const IAnimationNotifyHandler* _pHandler{ nullptr };

        /** @brief 이름의 인자입니다. 처리기가 선언하지 않은 이름이면 nullptr 입니다. */
        const AnimationNotifyParam* findParam( const hashed_string& name ) const;
        /**
         * @brief 인자 값들입니다. 선언한 인자는 읽을 때 모두 채워지므로(적지 않은 선택 인자는 선언의 값) 늘 있습니다.
         *        처리기가 선언하지 않은 이름을 물으면 단언하고 형식의 0 값입니다(처리기 코드의 결함).
         */
        hashed_string getNameParam( const hashed_string& name ) const;
        string        getTextParam( const hashed_string& name ) const;
        float32       getFloatParam( const hashed_string& name ) const;
        float3        getVectorParam( const hashed_string& name ) const;
        bool          getBoolParam( const hashed_string& name ) const;

    private:
        /** @brief 선언한 인자를 찾습니다. 없으면 단언합니다. */
        const AnimationNotifyParam* findDeclaredParam( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 테이블 하나입니다. 파일 머리말을 참고하세요. */
    class SW_API AnimationNotifyTable
    {
    public:
        /** @brief XML 텍스트에서 읽습니다(지금 내용을 비우고). 처리기는 @p registry 에서 이름으로 찾습니다. 오류가 있으면 모두 로그로 내고 false 입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, const AnimationNotifyHandlerRegistry& registry );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, const AnimationNotifyHandlerRegistry& registry );

        /** @brief 알림 이름의 항목을 반환합니다. 없으면 nullptr 입니다(테이블에 없는 알림은 처리하지 않으며, 게임 코드가 목록을 직접 읽을 수 있습니다). */
        const AnimationNotifyEntry* findEntry( const hashed_string& notify ) const;
        /** @brief 줄들입니다. */
        const vector<AnimationNotifyEntry>& getEntries() const { return _listEntry; }
        /** @brief 발 닿음 감지 줄들입니다(`<FootContact>`). */
        const vector<AnimationFootContactDef>& getFootContacts() const { return _listFootContact; }
        /** @brief 그 알림을 발 닿음 감지가 내는지 반환합니다. 그러면 클립의 같은 이름 알림은 처리하지 않습니다. */
        bool isFootContactNotify( const hashed_string& notify ) const;

    private:
        void readRoot( const XMLNode& root, const AnimationNotifyHandlerRegistry& registry, CharacterDataReader& reader );
        void readEntry( const XMLNode& node, const AnimationNotifyHandlerRegistry& registry, CharacterDataReader& reader );
        void readFootContact( const XMLNode& node, CharacterDataReader& reader );

        vector<AnimationNotifyEntry>    _listEntry;
        vector<AnimationFootContactDef> _listFootContact;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 열린 구간 알림 하나의 상태 슬롯입니다(처리기가 시작에서 채우고 틱마다 읽습니다). 처리기는 상태를 갖지 않으므로 여기에 둡니다.
     * @details 칼 궤적 판정은 지난 칼날 표본점(비었으면 아직 없다)과 이미 맞힌 오브젝트를(`MeleeHitWindowUtil::sweep` 의 상태), 모션 워핑은 창의 끝 시각을 적는다.
     */
    struct AnimationNotifyStateData
    {
        vector<float3>           _listPreviousPoint{};
        vector<GameObjectHandle> _listHitTarget{};
        float32                  _elapsed{ 0.0f };
        float32                  _endTime{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기 한 번의 호출 문맥입니다. */
    struct AnimationNotifyContext
    {
        AnimationNotifyComponent&   _component;
        GameObject&                 _owner;
        const AnimationNotifyEntry& _entry;
        const AnimationFiredNotify& _fired;
        AnimationNotifyStateData*   _pState{ nullptr }; ///< 구간 알림이면 그 상태 슬롯이고, 아니면 nullptr 이다.
        float32                     _deltaSeconds{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAnimationNotifyHandler
     * @brief 알림 처리기 종류 하나입니다. 상태를 갖지 않습니다(구간의 상태는 `AnimationNotifyStateData`). 게임 스레드에서 불립니다(틱 밖).
     * @details 길이 없는 알림은 `onNotify`, 구간 알림은 `onNotifyBegin` → 프레임마다 `onNotifyTick` → `onNotifyEnd` 입니다. 구간을 받지 않는 처리기
     *          (`supportsState` 거짓)에 구간 알림이 오면 시작에서 `onNotify` 한 번입니다. 구간 처리기도 길이 없는 알림은 `onNotify` 로 받습니다
     *          (칼 궤적은 한 순간의 칼날 판정, 이벤트는 순간 이벤트).
     */
    class SW_API IAnimationNotifyHandler
    {
    public:
        IAnimationNotifyHandler()                                            = default;
        virtual ~IAnimationNotifyHandler()                                   = default;
        IAnimationNotifyHandler( const IAnimationNotifyHandler& )            = delete;
        IAnimationNotifyHandler& operator=( const IAnimationNotifyHandler& ) = delete;

        /** @brief 받는 인자 선언입니다. 테이블 항목이 이 밖의 인자를 쓰면 읽기 오류입니다. */
        virtual vector_reference<const AnimationNotifyParamDef> getParams() const = 0;
        /** @brief 구간 알림(시작, 틱, 끝)을 받는지 반환합니다. */
        virtual bool supportsState() const { return false; }
        /** @brief 길이 없는 알림이 울렸습니다. */
        virtual void onNotify( AnimationNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 열렸습니다. */
        virtual void onNotifyBegin( AnimationNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 열린 채 한 프레임이 지났습니다. */
        virtual void onNotifyTick( AnimationNotifyContext& context ) const { (void)context; }
        /** @brief 구간이 닫혔을 때 호출합니다(끝 시각을 지났거나, 클립이 빠졌거나, 컴포넌트가 끝났을 때). */
        virtual void onNotifyEnd( AnimationNotifyContext& context ) const { (void)context; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationNotifyHandlerRegistry
     * @brief 이름에서 처리기로 가는 테이블입니다. `getDefault` 가 엔진 내장 처리기(PlaySound, SpawnPrefab, Footstep, HitWindow, CameraShake, GameplayEvent,
     *        MotionWarp)를 가지고 있습니다. 추가한 처리기는 엔진이 끝날 때까지 살아 있으므로, 핫 리로드되는 모듈의 처리기는 추가하지 말고 이벤트(`GameplayEvent`)로 받으세요.
     */
    class SW_API AnimationNotifyHandlerRegistry
    {
    public:
        AnimationNotifyHandlerRegistry();
        ~AnimationNotifyHandlerRegistry();

        AnimationNotifyHandlerRegistry( const AnimationNotifyHandlerRegistry& )            = delete;
        AnimationNotifyHandlerRegistry& operator=( const AnimationNotifyHandlerRegistry& ) = delete;

        /** @brief 내장 처리기를 든 표입니다(엔진 수명). */
        static const AnimationNotifyHandlerRegistry& getDefault();

        /** @brief 처리기를 이름으로 추가합니다. 같은 이름이 있으면 교체하며, 그 처리기를 가리키던 테이블은 다시 읽어야 합니다. */
        void registerHandler( const hashed_string& name, unique_ptr<IAnimationNotifyHandler> handler );
        /** @brief 이름의 처리기입니다. 없으면 nullptr 입니다. */
        const IAnimationNotifyHandler* findHandler( const hashed_string& name ) const;

    private:
        NameRegistry<unique_ptr<IAnimationNotifyHandler>> _registry; ///< 이름 → 처리기(등록 순서)
    };
} // namespace sw
