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

bool EffectsActionsController::doRenderMidiTrack(const trackedit::TrackId& trackId)
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
    const muse::Ret ret = effectExecutionScenario()->performEffect(
        EffectId::fromStdString(effectId), std::string());
    // don't leak notes into a later manual Generate if the effect failed early
    MidiRenderQueue::Set({});

    if (!ret) {
        LOGE() << "MIDI render failed: effectId=" << effectId << ", code=" << ret.code() << ", text=" << ret.text();
        return false;
    }
    return true;
}
