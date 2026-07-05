/*
* Audacity: A Digital Audio Editor
*/
#include "pianorollcanvas.h"

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QPainter>

#include "au3-wave-track/MidiInstrument.h"
#include "au3-wave-track/MidiSequence.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"

#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"
#include "au3wrap/au3types.h"

#include "trackedit/itrackeditproject.h"

using namespace au::projectscene;

static constexpr double KEYBOARD_W = 48.0;
static constexpr double ROW_H = 14.0;
static constexpr double EDGE_HIT_PX = 6.0;
static constexpr double MIN_PPB = 16.0;
static constexpr double MAX_PPB = 256.0;
static constexpr int MIN_TOP_PITCH = 24;
static constexpr int MAX_TOP_PITCH = 127;

static bool isBlackKey(int pitch)
{
    switch (pitch % 12) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

PianoRollCanvas::PianoRollCanvas(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);

    m_renderDebounce.setSingleShot(true);
    m_renderDebounce.setInterval(1200);
    connect(&m_renderDebounce, &QTimer::timeout, this, [this]() {
        // silently skip if no instrument is assigned yet: an auto render
        // must not nag with the "no instrument" error dialog
        const WaveTrack* track = waveTrack();
        if (track && !MidiInstrument::Get(*track).EffectId().empty()) {
            requestRender();
        }
    });
}

PianoRollCanvas::~PianoRollCanvas() = default;

void PianoRollCanvas::componentComplete()
{
    QQuickPaintedItem::componentComplete();

    projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) {
        // undo/redo may change the notes under us
        update();
    });
}

QString PianoRollCanvas::trackId() const
{
    return m_trackId;
}

void PianoRollCanvas::setTrackId(const QString& trackId)
{
    if (m_trackId == trackId) {
        return;
    }
    m_trackId = trackId;
    emit trackIdChanged();
    update();
}

int PianoRollCanvas::gridDivision() const
{
    return m_gridDivision;
}

void PianoRollCanvas::setGridDivision(int division)
{
    division = std::clamp(division, 1, 16);
    if (m_gridDivision == division) {
        return;
    }
    m_gridDivision = division;
    emit gridDivisionChanged();
    update();
}

double PianoRollCanvas::pixelsPerBeat() const
{
    return m_pixelsPerBeat;
}

void PianoRollCanvas::setPixelsPerBeat(double value)
{
    value = std::clamp(value, MIN_PPB, MAX_PPB);
    if (qFuzzyCompare(m_pixelsPerBeat, value)) {
        return;
    }
    m_pixelsPerBeat = value;
    emit pixelsPerBeatChanged();
    update();
}

void PianoRollCanvas::zoomIn()
{
    setPixelsPerBeat(m_pixelsPerBeat * 1.25);
}

void PianoRollCanvas::zoomOut()
{
    setPixelsPerBeat(m_pixelsPerBeat / 1.25);
}

bool PianoRollCanvas::autoRender() const
{
    return m_autoRender;
}

void PianoRollCanvas::setAutoRender(bool value)
{
    if (m_autoRender == value) {
        return;
    }
    m_autoRender = value;
    if (!value) {
        m_renderDebounce.stop();
    }
    emit autoRenderChanged();
}

void PianoRollCanvas::requestRender()
{
    if (m_trackId.isEmpty()) {
        return;
    }

    dispatcher()->dispatch("midi-render",
                           muse::actions::ActionData::make_arg1<int64_t>(m_trackId.toLongLong()));
}

void PianoRollCanvas::requestInstrumentUi()
{
    if (m_trackId.isEmpty()) {
        return;
    }

    m_renderDebounce.stop(); // the dialog flow renders by itself
    dispatcher()->dispatch("midi-open-instrument-ui",
                           muse::actions::ActionData::make_arg1<int64_t>(m_trackId.toLongLong()));
}

WaveTrack* PianoRollCanvas::waveTrack() const
{
    const auto project = globalContext()->currentProject();
    if (!project || m_trackId.isEmpty()) {
        return nullptr;
    }
    const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
    return au::au3::DomAccessor::findWaveTrack(*au3Project, ::TrackId(m_trackId.toLongLong()));
}

std::vector<MidiNote> PianoRollCanvas::currentNotes() const
{
    if (m_gesture != Gesture::None) {
        return m_gestureNotes;
    }
    if (const WaveTrack* track = waveTrack()) {
        return MidiSequence::Get(*track).Notes();
    }
    return {};
}

void PianoRollCanvas::commitNotes(std::vector<MidiNote> notes)
{
    WaveTrack* track = waveTrack();
    if (!track) {
        return;
    }

    std::sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeats < b.startBeats;
    });
    MidiSequence::Get(*track).SetNotes(std::move(notes));

    projectHistory()->pushHistoryState("Edited MIDI notes", "MIDI note edit");

    // repaint the mini preview in the timeline clip
    if (const auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutTrackChanged(au::au3::DomConverter::track(track));
    }

    if (m_autoRender) {
        m_renderDebounce.start();
    }
}

double PianoRollCanvas::beatToX(double beat) const
{
    return KEYBOARD_W + (beat - m_scrollBeats) * m_pixelsPerBeat;
}

double PianoRollCanvas::xToBeat(double x) const
{
    return m_scrollBeats + (x - KEYBOARD_W) / m_pixelsPerBeat;
}

double PianoRollCanvas::pitchToY(int pitch) const
{
    return (m_topPitch - pitch) * ROW_H;
}

int PianoRollCanvas::yToPitch(double y) const
{
    return m_topPitch - static_cast<int>(std::floor(y / ROW_H));
}

double PianoRollCanvas::snapFloor(double beat) const
{
    return std::floor(beat / gridStep()) * gridStep();
}

std::optional<size_t> PianoRollCanvas::hitTest(const std::vector<MidiNote>& notes, double x, double y, bool& edge) const
{
    const double beat = xToBeat(x);
    const int pitch = yToPitch(y);
    edge = false;

    // iterate backwards so the visually topmost (last drawn) note wins
    for (size_t i = notes.size(); i > 0; --i) {
        const MidiNote& note = notes[i - 1];
        if (note.pitch != pitch) {
            continue;
        }
        const double endX = beatToX(note.startBeats + note.lengthBeats);
        if (beat >= note.startBeats && beat <= note.startBeats + note.lengthBeats) {
            edge = (endX - x) <= EDGE_HIT_PX;
            return i - 1;
        }
        // a bit past the right edge still counts as edge grab
        if (x > endX && x - endX <= EDGE_HIT_PX / 2) {
            edge = true;
            return i - 1;
        }
    }
    return std::nullopt;
}

void PianoRollCanvas::paint(QPainter* painter)
{
    const QColor bgColor(0x28, 0x2B, 0x33);
    const QColor blackRowColor(0x22, 0x25, 0x2C);
    const QColor gridFaint(255, 255, 255, 14);
    const QColor gridBeat(255, 255, 255, 34);
    const QColor gridBar(255, 255, 255, 70);
    const QColor octaveLine(255, 255, 255, 26);
    const QColor noteColor(0x5C, 0xB5, 0xEF);
    const QColor noteBorder(0x1B, 0x40, 0x59);
    const QColor whiteKey(0xE8, 0xE8, 0xE8);
    const QColor blackKey(0x30, 0x30, 0x30);
    const QColor keyText(0x60, 0x60, 0x60);

    painter->fillRect(QRectF(0, 0, width(), height()), bgColor);

    const int visibleRows = static_cast<int>(std::ceil(height() / ROW_H)) + 1;
    const int bottomPitch = std::max(0, m_topPitch - visibleRows);

    // rows for black keys
    for (int pitch = bottomPitch; pitch <= m_topPitch; ++pitch) {
        const double y = pitchToY(pitch);
        if (isBlackKey(pitch)) {
            painter->fillRect(QRectF(KEYBOARD_W, y, width() - KEYBOARD_W, ROW_H), blackRowColor);
        }
        if (pitch % 12 == 0) { // line under every C
            painter->fillRect(QRectF(KEYBOARD_W, y + ROW_H - 1, width() - KEYBOARD_W, 1), octaveLine);
        }
    }

    // vertical grid: steps, beats, bars
    double quartersPerBar = 4.0;
    if (const auto project = globalContext()->currentProject()) {
        const auto au3Project = reinterpret_cast<au::au3::Au3Project*>(project->au3ProjectPtr());
        const auto& ts = ProjectTimeSignature::Get(*au3Project);
        quartersPerBar = ts.GetUpperTimeSignature() * 4.0 / ts.GetLowerTimeSignature();
    }

    const double step = gridStep();
    const double firstBeat = std::max(0.0, snapFloor(xToBeat(KEYBOARD_W)));
    const double lastBeat = xToBeat(width());
    for (double b = firstBeat; b <= lastBeat; b += step) {
        const double x = beatToX(b);
        if (x < KEYBOARD_W) {
            continue;
        }
        const double barPos = std::fmod(b, quartersPerBar);
        const double beatPos = std::fmod(b, 1.0);
        QColor color = gridFaint;
        if (std::abs(barPos) < step / 2) {
            color = gridBar;
        } else if (std::abs(beatPos) < step / 2) {
            color = gridBeat;
        }
        painter->fillRect(QRectF(x, 0, 1, height()), color);
    }

    // notes
    const std::vector<MidiNote> notes = currentNotes();
    for (size_t i = 0; i < notes.size(); ++i) {
        const MidiNote& note = notes[i];
        if (note.pitch < bottomPitch || note.pitch > m_topPitch) {
            continue;
        }
        const double x = beatToX(note.startBeats);
        const double w = std::max(2.0, note.lengthBeats * m_pixelsPerBeat - 1.0);
        if (x + w < KEYBOARD_W || x > width()) {
            continue;
        }
        const double y = pitchToY(note.pitch);

        QColor fill = noteColor;
        fill.setAlphaF(0.55f + 0.45f * std::clamp(note.velocity, 0.0f, 1.0f));
        const bool active = (m_gesture != Gesture::None && i == m_gestureIndex);
        if (active) {
            fill = fill.lighter(130);
        }

        QRectF rect(x, y + 1.5, w, ROW_H - 3.0);
        rect.setLeft(std::max(rect.left(), KEYBOARD_W));
        painter->setPen(QPen(active ? fill.lighter(150) : noteBorder, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(rect, 2, 2);
    }

    // piano keys strip on top of everything
    painter->setPen(Qt::NoPen);
    for (int pitch = bottomPitch; pitch <= m_topPitch; ++pitch) {
        const double y = pitchToY(pitch);
        painter->fillRect(QRectF(0, y, KEYBOARD_W, ROW_H), isBlackKey(pitch) ? blackKey : whiteKey);
        painter->fillRect(QRectF(0, y + ROW_H - 1, KEYBOARD_W, 1), QColor(0, 0, 0, 60));
        if (pitch % 12 == 0) {
            painter->setPen(keyText);
            QFont font = painter->font();
            font.setPixelSize(9);
            painter->setFont(font);
            // MIDI 60 = C4 in the common convention
            painter->drawText(QRectF(0, y, KEYBOARD_W - 4, ROW_H),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QStringLiteral("C%1").arg(pitch / 12 - 1));
            painter->setPen(Qt::NoPen);
        }
    }
    painter->setPen(QPen(QColor(0, 0, 0, 120), 1));
    painter->drawLine(QPointF(KEYBOARD_W, 0), QPointF(KEYBOARD_W, height()));
}

void PianoRollCanvas::mousePressEvent(QMouseEvent* event)
{
    const double x = event->position().x();
    const double y = event->position().y();

    if (x <= KEYBOARD_W) {
        event->accept();
        return;
    }

    std::vector<MidiNote> notes = currentNotes();
    bool edge = false;
    const auto hit = hitTest(notes, x, y, edge);

    if (event->button() == Qt::RightButton) {
        if (hit.has_value()) {
            notes.erase(notes.begin() + static_cast<ptrdiff_t>(hit.value()));
            commitNotes(std::move(notes));
            update();
        }
        event->accept();
        return;
    }

    if (hit.has_value()) {
        m_gesture = edge ? Gesture::Resize : Gesture::Move;
        m_gestureNotes = std::move(notes);
        m_gestureIndex = hit.value();
        m_grabBeatOffset = xToBeat(x) - m_gestureNotes[m_gestureIndex].startBeats;
        m_gestureModified = false;
    } else {
        // draw a new note; drag continues as move
        MidiNote note;
        note.startBeats = std::max(0.0, snapFloor(xToBeat(x)));
        note.lengthBeats = gridStep();
        note.pitch = std::clamp(yToPitch(y), 0, 127);
        note.velocity = 0.8f;

        m_gestureNotes = std::move(notes);
        m_gestureNotes.push_back(note);
        m_gestureIndex = m_gestureNotes.size() - 1;
        m_gesture = Gesture::Move;
        m_grabBeatOffset = 0.0;
        m_gestureModified = true; // creation itself is a change
    }

    update();
    event->accept();
}

void PianoRollCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_gesture == Gesture::None) {
        return;
    }

    MidiNote& note = m_gestureNotes[m_gestureIndex];
    const double beat = xToBeat(event->position().x());

    if (m_gesture == Gesture::Move) {
        const double newStart = std::max(0.0, snapFloor(beat - m_grabBeatOffset + gridStep() / 2));
        const int newPitch = std::clamp(yToPitch(event->position().y()), 0, 127);
        if (!qFuzzyCompare(newStart, note.startBeats) || newPitch != note.pitch) {
            note.startBeats = newStart;
            note.pitch = newPitch;
            m_gestureModified = true;
            update();
        }
    } else if (m_gesture == Gesture::Resize) {
        const double snapped = std::ceil((beat - note.startBeats) / gridStep()) * gridStep();
        const double newLength = std::max(gridStep(), snapped);
        if (!qFuzzyCompare(newLength, note.lengthBeats)) {
            note.lengthBeats = newLength;
            m_gestureModified = true;
            update();
        }
    }

    event->accept();
}

void PianoRollCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_gesture == Gesture::None) {
        return;
    }

    if (m_gestureModified) {
        commitNotes(m_gestureNotes);
    }
    m_gesture = Gesture::None;
    m_gestureNotes.clear();
    update();
    event->accept();
}

void PianoRollCanvas::hoverMoveEvent(QHoverEvent* event)
{
    const double x = event->position().x();
    const double y = event->position().y();
    if (x <= KEYBOARD_W) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const std::vector<MidiNote> notes = currentNotes();
    bool edge = false;
    const auto hit = hitTest(notes, x, y, edge);
    if (hit.has_value() && edge) {
        setCursor(Qt::SizeHorCursor);
    } else if (hit.has_value()) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void PianoRollCanvas::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        // zoom anchored at the cursor position
        const double anchorBeat = xToBeat(event->position().x());
        const double factor = std::pow(1.25, steps);
        setPixelsPerBeat(m_pixelsPerBeat * factor);
        m_scrollBeats = std::max(0.0, anchorBeat - (event->position().x() - KEYBOARD_W) / m_pixelsPerBeat);
    } else if (event->modifiers() & Qt::ShiftModifier) {
        m_scrollBeats = std::max(0.0, m_scrollBeats - steps * gridStep() * 4);
    } else {
        m_topPitch = std::clamp(m_topPitch + static_cast<int>(steps > 0 ? std::ceil(steps) : std::floor(steps)) * 2,
                                MIN_TOP_PITCH, MAX_TOP_PITCH);
    }

    update();
    event->accept();
}
