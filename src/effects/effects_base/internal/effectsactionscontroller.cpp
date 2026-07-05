/*
* Audacity: A Digital Audio Editor
*/
#include "effectsactionscontroller.h"
#include "effects/effects_base/effectstypes.h"
#include "effects/effects_base/internal/effectsutils.h"
#include "effectsuiactions.h"

#include <algorithm>

#include "spectrogram/spectrogramtypes.h"
#include "wx/string.h"

#include "au3-components/EffectAutomationParameters.h"
#include "au3-effects/MidiRenderQueue.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"
#include "au3-project-rate/ProjectRate.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-wave-track/MidiInstrument.h"
#include "au3-wave-track/MidiSequence.h"
#include "au3-wave-track/WaveTrack.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/au3types.h"

#include "project/iaudacityproject.h"

#include "translation.h"

#include "log.h"

using namespace muse::actions;
using namespace au::effects;

void EffectsActionsController::init()
{
    m_uiActions = std::make_shared<EffectsUiActions>(iocContext(), this);

    effectsProvider()->effectMetaListChanged().onNotify(this, [this](){
        registerActions();
    });

    registerActions();

    effectExecutionScenario()->lastProcessorIsNowAvailable().onNotify(this, [this] {
        m_canReceiveActionsChanged.send({ "repeat-last-effect" });
    });

    frequencySelectionController()->frequencySelectionChanged().onReceive(this, [this](bool complete) {
        if (complete) {
            notifyAboutSpectralEffectsAvailability();
        }
    });

    // live MIDI: (re)schedule the notes of live-enabled MIDI tracks into
    // their realtime instrument instances whenever playback starts
    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        if (playbackController()->isPlaying()) {
            scheduleLiveMidiNotes();
        }
    });
}

void EffectsActionsController::notifyAboutSpectralEffectsAvailability()
{
    ActionCodeList codes;
    const auto spectralEffects = spectralEffectsRegister()->spectralEffects();
    for (const auto& spectralEffect : spectralEffects) {
        codes.push_back(spectralEffect.action);
    }
    m_canReceiveActionsChanged.send(codes);
}

void EffectsActionsController::registerActions()
{
    dispatcher()->unReg(this);

    EffectMetaList effects = effectsProvider()->effectMetaList();
    for (const EffectMeta& e : effects) {
        dispatcher()->reg(this, ActionQuery(makeEffectAction(EFFECT_OPEN_ACTION, e.id)), [this](const ActionQuery& q) {
            onEffectTriggered(q);
        });
    }

    dispatcher()->reg(this, "repeat-last-effect", this, &EffectsActionsController::repeatLastEffect);
    dispatcher()->reg(this, "plugin-manager", this, &EffectsActionsController::openPluginManager);

    // presets
    dispatcher()->reg(this, ActionQuery("action://effects/presets/apply"), this, &EffectsActionsController::applyPreset);
    dispatcher()->reg(this, ActionQuery("action://effects/presets/save"), this, &EffectsActionsController::savePreset);
    dispatcher()->reg(this, ActionQuery("action://effects/presets/save_as"), this, &EffectsActionsController::savePresetAs);
    dispatcher()->reg(this, ActionQuery("action://effects/presets/delete"), this, &EffectsActionsController::deletePreset);
    dispatcher()->reg(this, ActionQuery("action://effects/presets/import"), this, &EffectsActionsController::importPreset);
    dispatcher()->reg(this, ActionQuery("action://effects/presets/export"), this, &EffectsActionsController::exportPreset);

    dispatcher()->reg(this, ActionQuery("action://effects/apply"), this, &EffectsActionsController::applyEffect);
    dispatcher()->reg(this, ActionQuery("action://effects/toggle_vendor_ui"), this, &EffectsActionsController::toggleVendorUI);

    // MIDI track (AU4 DAW fork); args-based like clip-pitch-speed-open —
    // query-actions would need a UiAction per full query string
    dispatcher()->reg(this, "midi-set-instrument", this, &EffectsActionsController::setMidiInstrument);
    dispatcher()->reg(this, "midi-render", this, &EffectsActionsController::renderMidiTrack);
    dispatcher()->reg(this, "midi-open-instrument-ui", this, &EffectsActionsController::openMidiInstrumentUi);
    dispatcher()->reg(this, "midi-toggle-live", this, &EffectsActionsController::toggleMidiLive);
    dispatcher()->reg(this, "midi-audition-note", this, &EffectsActionsController::auditionMidiNote);

    m_uiActions->reload();
    uiActionsRegister()->unreg(m_uiActions);
    uiActionsRegister()->reg(m_uiActions);
}

void EffectsActionsController::onEffectTriggered(const muse::actions::ActionQuery& q)
{
    muse::String effectId = muse::String::fromStdString(q.param("effectId").toString());
    IF_ASSERT_FAILED(!effectId.empty()) {
        return;
    }
    playbackController()->stop();

    effectExecutionScenario()->performEffect(effectId);
}

void EffectsActionsController::applyEffect(const muse::actions::ActionQuery& q)
{
    const EffectId effectId = effectIdFromAction(q);
    IF_ASSERT_FAILED(!effectId.empty()) {
        return;
    }

    CommandParameters eap;
    for (const auto& [key, val] : q.params()) {
        if (key == "effectId") {
            continue;
        }
        eap.Write(wxString::FromUTF8(key), wxString::FromUTF8(val.toString()));
    }
    wxString params;
    eap.GetParameters(params);

    LOGI() << "applyEffect: effectId=" << effectId << ", params=" << params.ToStdString(wxConvUTF8);

    playbackController()->stop();
    const muse::Ret ret = effectExecutionScenario()->performEffect(effectId, params.ToStdString(wxConvUTF8));
    if (!ret) {
        LOGE() << "applyEffect failed: effectId=" << effectId << ", code=" << ret.code() << ", text=" << ret.text();
    }
}

void EffectsActionsController::repeatLastEffect()
{
    playbackController()->stop();

    effectExecutionScenario()->repeatLastProcessor();
}

void EffectsActionsController::applyPreset(const muse::actions::ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("instanceId") && q.contains("presetId")) {
        return;
    }

    EffectInstanceId effectInstanceId = q.param("instanceId").toInt();
    PresetId presetId = q.param("presetId").toString();
    presetsScenario()->loadPreset(effectInstanceId, presetId);
}

void EffectsActionsController::savePresetAs(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("instanceId")) {
        return;
    }

    EffectInstanceId effectInstanceId = q.param("instanceId").toInt();
    presetsScenario()->savePresetAs(effectInstanceId);
}

void EffectsActionsController::savePreset(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("instanceId") && q.contains("presetId")) {
        return;
    }

    const EffectInstanceId effectInstanceId = q.param("instanceId").toInt();
    const PresetId presetId = q.param("presetId").toString();
    presetsScenario()->savePreset(effectInstanceId, presetId);
}

void EffectsActionsController::deletePreset(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("effectId") && q.contains("presetId")) {
        return;
    }

    EffectId effectId = EffectId::fromStdString(q.param("effectId").toString());
    PresetId presetId = q.param("presetId").toString();
    presetsScenario()->deletePreset(effectId, presetId);
}

void EffectsActionsController::importPreset(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("instanceId")) {
        return;
    }

    EffectInstanceId effectInstanceId = q.param("instanceId").toInt();
    presetsScenario()->importPreset(effectInstanceId);
}

void EffectsActionsController::exportPreset(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("instanceId")) {
        return;
    }

    EffectInstanceId effectInstanceId = q.param("instanceId").toInt();
    presetsScenario()->exportPreset(effectInstanceId);
}

void EffectsActionsController::toggleVendorUI(const ActionQuery& q)
{
    IF_ASSERT_FAILED(q.contains("effectId")) {
        return;
    }

    const EffectId effectId = EffectId::fromStdString(q.param("effectId").toString());
    const EffectUIMode currentMode = configuration()->effectUIMode(effectId);
    const EffectUIMode newMode = (currentMode == EffectUIMode::VendorUI) ? EffectUIMode::FallbackUI : EffectUIMode::VendorUI;
    configuration()->setEffectUIMode(effectId, newMode);
}

bool EffectsActionsController::canReceiveAction(const muse::actions::ActionCode& code) const
{
    if (code == "repeat-last-effect") {
        return effectExecutionScenario()->lastProcessorIsAvailable();
    } else {
        const auto spectralEffects = spectralEffectsRegister()->spectralEffects();
        const auto it = std::find_if(spectralEffects.begin(), spectralEffects.end(), [&code](const auto& spectralEffect) {
            return spectralEffect.action == code;
        });
        if (it != spectralEffects.end()) {
            const spectrogram::FrequencySelection selection = frequencySelectionController()->frequencySelection();
            return frequencySelectionController()->showsSpectrogram(selection.trackId) && selection.isValid();
        }

        return true;
    }
}

muse::async::Channel<muse::actions::ActionCodeList> EffectsActionsController::canReceiveActionsChanged() const
{
    return m_canReceiveActionsChanged;
}

void EffectsActionsController::openPluginManager()
{
    interactive()->open("audacity://effects/plugin_manager");
}

void EffectsActionsController::setMidiInstrument(const muse::actions::ActionData& args)
{
    IF_ASSERT_FAILED(args.count() == 2) {
        return;
    }

    const trackedit::TrackId trackId = args.arg<trackedit::TrackId>(0);
    const std::string effectId = args.arg<std::string>(1);

    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
    if (!track || !track->IsMidi()) {
        return;
    }

    MidiInstrument::Get(*track).SetEffectId(effectId);
    projectHistory()->pushHistoryState("Set MIDI instrument", "Set MIDI instrument");

    doRenderMidiTrack(trackId);
}

void EffectsActionsController::renderMidiTrack(const muse::actions::ActionData& args)
{
    IF_ASSERT_FAILED(args.count() == 1) {
        return;
    }

    doRenderMidiTrack(args.arg<trackedit::TrackId>(0));
}

void EffectsActionsController::openMidiInstrumentUi(const muse::actions::ActionData& args)
{
    IF_ASSERT_FAILED(args.count() == 1) {
        return;
    }

    // render WITH the settings dialog: the vendor UI opens, Preview lets the
    // user listen while tweaking, OK applies and remembers the settings
    // (EffectManager keeps them per plugin, so silent renders reuse them)
    doRenderMidiTrack(args.arg<trackedit::TrackId>(0), true);
}

namespace {
//! The realtime instrument state on the track's effect stack, if live mode is on
std::shared_ptr<RealtimeEffectState> findLiveInstrumentState(WaveTrack& track)
{
    const std::string& effectId = MidiInstrument::Get(track).EffectId();
    if (effectId.empty()) {
        return nullptr;
    }
    auto& list = RealtimeEffectList::Get(track);
    for (size_t i = 0, count = list.GetStatesCount(); i < count; ++i) {
        auto state = list.GetStateAt(i);
        if (state && state->GetID().ToStdString() == effectId) {
            return state;
        }
    }
    return nullptr;
}
}

void EffectsActionsController::toggleMidiLive(const muse::actions::ActionData& args)
{
    IF_ASSERT_FAILED(args.count() == 1) {
        return;
    }
    const trackedit::TrackId trackId = args.arg<trackedit::TrackId>(0);

    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
    if (!track || !track->IsMidi()) {
        return;
    }

    const std::string& effectId = MidiInstrument::Get(*track).EffectId();
    if (effectId.empty()) {
        interactive()->error(muse::trc("effects", "No instrument assigned"),
                             muse::trc("effects", "Right-click the MIDI clip and pick an instrument first."));
        return;
    }

    if (const auto state = findLiveInstrumentState(*track)) {
        realtimeEffectService()->removeRealtimeEffect(trackId, state);
    } else {
        realtimeEffectService()->addRealtimeEffect(trackId, EffectId::fromStdString(effectId));
        if (playbackController()->isPlaying()) {
            scheduleLiveMidiNotes();
        }
    }
}

void EffectsActionsController::auditionMidiNote(const muse::actions::ActionData& args)
{
    IF_ASSERT_FAILED(args.count() == 2) {
        return;
    }
    const trackedit::TrackId trackId = args.arg<trackedit::TrackId>(0);
    const int pitch = args.arg<int>(1);

    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
    if (!track || !track->IsMidi()) {
        return;
    }

    // audible only while the realtime chain is processing (live mode + playback)
    const auto state = findLiveInstrumentState(*track);
    if (!state) {
        return;
    }
    const auto receiver = std::dynamic_pointer_cast<MidiRenderQueue::LiveMidiReceiver>(state->GetInstance());
    if (!receiver) {
        return;
    }

    const double rate = ProjectRate::Get(*au3Project).GetRate();
    receiver->QueueLiveNoteNow(static_cast<long long>(0.4 * rate), std::clamp(pitch, 0, 127), 0.8f);
}

void EffectsActionsController::scheduleLiveMidiNotes()
{
    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    const double rate = ProjectRate::Get(*au3Project).GetRate();
    const double quarterSec = ProjectTimeSignature::Get(*au3Project).GetQuarterDuration();
    const double playStartSec = globalContext()->playbackState()->playbackPosition();

    auto& tracks = au::au3::Au3TrackList::Get(*au3Project);
    for (auto au3Track : tracks.Any<WaveTrack>()) {
        if (!au3Track->IsMidi()) {
            continue;
        }
        const auto state = findLiveInstrumentState(*au3Track);
        if (!state) {
            continue;
        }
        const auto receiver = std::dynamic_pointer_cast<MidiRenderQueue::LiveMidiReceiver>(state->GetInstance());
        if (!receiver) {
            continue;
        }

        double anchorSec = 0.0;
        for (const auto& interval : au3Track->Intervals()) {
            anchorSec = interval->GetPlayStartTime();
            break;
        }

        receiver->ResetLiveNotes();
        for (const MidiNote& note : MidiSequence::Get(*au3Track).Notes()) {
            const double absSec = anchorSec + note.startBeats * quarterSec;
            const double durSec = note.lengthBeats * quarterSec;
            if (absSec + durSec <= playStartSec) {
                continue; // already in the past
            }
            const auto sampleTime = static_cast<long long>(
                std::max(0.0, (absSec - playStartSec) * rate));
            receiver->QueueLiveNote(sampleTime,
                                    static_cast<long long>(durSec * rate),
                                    note.pitch, note.velocity);
        }
    }
}

bool EffectsActionsController::doRenderMidiTrack(const trackedit::TrackId& trackId, bool withDialog)
{
    const auto project = globalContext()->currentProject();
    if (!project) {
        return false;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(trackId));
    if (!track || !track->IsMidi()) {
        return false;
    }

    const std::string& effectId = MidiInstrument::Get(*track).EffectId();
    if (effectId.empty()) {
        interactive()->error(muse::trc("effects", "No instrument assigned"),
                             muse::trc("effects", "Right-click the MIDI clip and pick an instrument first."));
        return false;
    }

    const std::vector<MidiNote>& notes = MidiSequence::Get(*track).Notes();
    if (notes.empty()) {
        return false;
    }

    const double quarterSec = ProjectTimeSignature::Get(*au3Project).GetQuarterDuration();

    std::vector<MidiRenderQueue::Note> renderNotes;
    renderNotes.reserve(notes.size());
    double endSec = 0.0;
    for (const MidiNote& note : notes) {
        MidiRenderQueue::Note rn;
        rn.timeSec = note.startBeats * quarterSec;
        rn.durationSec = note.lengthBeats * quarterSec;
        rn.pitch = note.pitch;
        rn.velocity = note.velocity;
        endSec = std::max(endSec, rn.timeSec + rn.durationSec);
        renderNotes.push_back(rn);
    }
    //let releases/reverb of the instrument ring out a little
    constexpr double releaseTailSec = 0.5;
    endSec += releaseTailSec;

    // notes are relative to the clip: render at the clip's current position
    double anchorSec = 0.0;
    for (const auto& interval : track->Intervals()) {
        anchorSec = interval->GetPlayStartTime();
        break;
    }

    playbackController()->stop();

    // the generator pipeline renders into the selected region of the track
    selectionController()->resetSelectedClips();
    selectionController()->setSelectedTracks({ trackId });
    selectionController()->setDataSelectedStartTime(anchorSec, true);
    selectionController()->setDataSelectedEndTime(anchorSec + endSec, true);

    MidiRenderQueue::Set(std::move(renderNotes));
    const EffectId effectIdString = EffectId::fromStdString(effectId);
    // the single-argument overload shows the settings dialog (vendor UI),
    // the params overload applies silently with the remembered settings
    const muse::Ret ret = withDialog
                          ? effectExecutionScenario()->performEffect(effectIdString)
                          : effectExecutionScenario()->performEffect(effectIdString, std::string());
    // don't leak notes into a later manual Generate
    MidiRenderQueue::Set({});

    if (!ret) {
        LOGE() << "MIDI render failed: effectId=" << effectId << ", code=" << ret.code() << ", text=" << ret.text();
        return false;
    }
    return true;
}
