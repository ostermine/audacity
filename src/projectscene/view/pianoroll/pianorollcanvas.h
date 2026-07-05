/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <optional>
#include <vector>

#include <QQuickPaintedItem>
#include <QTimer>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"
#include "framework/actions/iactionsdispatcher.h"
#include "global/async/asyncable.h"

struct MidiNote;
class WaveTrack;

namespace au::projectscene {
//! Piano roll editor canvas: piano keys strip, beat grid and editable notes.
//! Works in quarter-note units (the storage unit of MidiSequence); never
//! converts to seconds, so tempo changes don't affect editing.
class PianoRollCanvas : public QQuickPaintedItem, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(int gridDivision READ gridDivision WRITE setGridDivision NOTIFY gridDivisionChanged FINAL)
    Q_PROPERTY(double pixelsPerBeat READ pixelsPerBeat WRITE setPixelsPerBeat NOTIFY pixelsPerBeatChanged FINAL)
    Q_PROPERTY(bool autoRender READ autoRender WRITE setAutoRender NOTIFY autoRenderChanged FINAL)

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };

public:
    PianoRollCanvas(QQuickItem* parent = nullptr);
    ~PianoRollCanvas() override; // out of line: members need the complete MidiNote

    QString trackId() const;
    void setTrackId(const QString& trackId);
    int gridDivision() const;
    void setGridDivision(int division);
    double pixelsPerBeat() const;
    void setPixelsPerBeat(double value);
    bool autoRender() const;
    void setAutoRender(bool value);

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void requestRender();

    void paint(QPainter* painter) override;

signals:
    void trackIdChanged();
    void gridDivisionChanged();
    void pixelsPerBeatChanged();
    void autoRenderChanged();

protected:
    void componentComplete() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Gesture {
        None,
        Move,
        Resize,
    };

    WaveTrack* waveTrack() const;
    std::vector<MidiNote> currentNotes() const;
    void commitNotes(std::vector<MidiNote> notes);

    double gridStep() const { return 1.0 / m_gridDivision; }
    double beatToX(double beat) const;
    double xToBeat(double x) const;
    double pitchToY(int pitch) const;
    int yToPitch(double y) const;
    double snapFloor(double beat) const;

    //! index of the note under the point; edge=true if near its right edge
    std::optional<size_t> hitTest(const std::vector<MidiNote>& notes, double x, double y, bool& edge) const;

    QString m_trackId;
    int m_gridDivision = 4; // 1/16 with quarter-note beats
    double m_pixelsPerBeat = 64.0;
    double m_scrollBeats = 0.0;
    int m_topPitch = 84; // C6 at the top row by default

    Gesture m_gesture = Gesture::None;
    std::vector<MidiNote> m_gestureNotes;
    size_t m_gestureIndex = 0;
    double m_grabBeatOffset = 0.0;
    bool m_gestureModified = false;

    bool m_autoRender = true;
    QTimer m_renderDebounce;
};
}
