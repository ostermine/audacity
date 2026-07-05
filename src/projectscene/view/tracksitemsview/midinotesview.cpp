/*
* Audacity: A Digital Audio Editor
*/
#include "midinotesview.h"

#include <algorithm>

#include <QPainter>

#include "au3-wave-track/MidiSequence.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/au3types.h"

#include "trackedit/itrackeditproject.h"

using namespace au::projectscene;

static constexpr int PITCH_PADDING = 2;    // semitones of air above/below the used range
static constexpr int MIN_PITCH_RANGE = 12; // never zoom rows beyond one octave

MidiNotesView::MidiNotesView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

TimelineContext* MidiNotesView::timelineContext() const
{
    return m_context;
}

void MidiNotesView::setTimelineContext(TimelineContext* newContext)
{
    if (m_context == newContext) {
        return;
    }

    if (m_context) {
        disconnect(m_context, nullptr, this, nullptr);
    }

    m_context = newContext;

    if (m_context) {
        connect(m_context, &TimelineContext::frameTimeChanged, this, [this]() { update(); });
        connect(m_context, &TimelineContext::zoomChanged, this, [this]() { update(); });
    }

    emit timelineContextChanged();
    update();
}

ClipKey MidiNotesView::clipKey() const
{
    return m_clipKey;
}

void MidiNotesView::setClipKey(const ClipKey& newClipKey)
{
    m_clipKey = newClipKey;
    emit clipKeyChanged();

    updateIsMidi();

    // the piano roll edits notes without touching the clip itself;
    // it announces them as a track change
    if (const auto prj = globalContext()->currentTrackeditProject()) {
        prj->trackChanged().onReceive(this, [this](const trackedit::Track& track) {
            if (track.id == m_clipKey.key.trackId) {
                update();
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    update();
}

ClipTime MidiNotesView::clipTime() const
{
    return m_clipTime;
}

void MidiNotesView::setClipTime(const ClipTime& newClipTime)
{
    if (m_clipTime == newClipTime) {
        return;
    }

    m_clipTime = newClipTime;
    emit clipTimeChanged();
    update();
}

QColor MidiNotesView::noteColor() const
{
    return m_noteColor;
}

void MidiNotesView::setNoteColor(const QColor& newColor)
{
    if (m_noteColor == newColor) {
        return;
    }

    m_noteColor = newColor;
    emit noteColorChanged();
    update();
}

bool MidiNotesView::isMidi() const
{
    return m_isMidi;
}

void MidiNotesView::updateIsMidi()
{
    bool result = false;

    const auto project = globalContext()->currentProject();
    if (project && m_clipKey.key.isValid()) {
        const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
        const WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(m_clipKey.key.trackId));
        result = track && track->IsMidi();
    }

    if (m_isMidi != result) {
        m_isMidi = result;
        emit isMidiChanged();
    }
}

void MidiNotesView::paint(QPainter* painter)
{
    if (!m_context || !m_clipKey.key.isValid()) {
        return;
    }

    const auto project = globalContext()->currentProject();
    if (!project) {
        return;
    }

    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    const WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(m_clipKey.key.trackId));
    if (!track || !track->IsMidi()) {
        return;
    }

    const std::vector<MidiNote>& notes = MidiSequence::Get(*track).Notes();
    if (notes.empty()) {
        return;
    }

    const double quarterSec = ProjectTimeSignature::Get(*au3Project).GetQuarterDuration();
    const double zoom = m_context->zoom(); // pixels per second

    int minPitch = 127;
    int maxPitch = 0;
    for (const MidiNote& note : notes) {
        minPitch = std::min(minPitch, note.pitch);
        maxPitch = std::max(maxPitch, note.pitch);
    }
    minPitch -= PITCH_PADDING;
    maxPitch += PITCH_PADDING;
    if (maxPitch - minPitch + 1 < MIN_PITCH_RANGE) {
        const int deficit = MIN_PITCH_RANGE - (maxPitch - minPitch + 1);
        minPitch -= deficit / 2;
        maxPitch += deficit - deficit / 2;
    }
    minPitch = std::max(0, minPitch);
    maxPitch = std::min(127, maxPitch);

    const double rowHeight = height() / double(maxPitch - minPitch + 1);
    const double barHeight = std::max(1.5, rowHeight * 0.8);

    for (const MidiNote& note : notes) {
        const double startSec = note.startBeats * quarterSec;
        const double lengthSec = note.lengthBeats * quarterSec;

        const double x = (startSec - m_clipTime.itemStartTime) * zoom;
        const double w = std::max(1.0, lengthSec * zoom - 1.0);
        if (x + w < 0 || x > width()) {
            continue;
        }

        const double y = (maxPitch - note.pitch) * rowHeight + (rowHeight - barHeight) / 2.0;

        QColor color = m_noteColor;
        color.setAlphaF(0.35f + 0.65f * std::clamp(note.velocity, 0.0f, 1.0f));
        painter->fillRect(QRectF(x, y, w, barHeight), color);
    }
}
