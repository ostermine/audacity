/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QQuickPaintedItem>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "global/async/asyncable.h"

#include "../timeline/timelinecontext.h"
#include "types/projectscenetypes.h"

namespace au::projectscene {
//! Mini piano roll preview of a MIDI-flagged track's notes, drawn on top of
//! the clip's (silent) waveform on the timeline
class MidiNotesView : public QQuickPaintedItem, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_PROPERTY(TimelineContext * context READ timelineContext WRITE setTimelineContext NOTIFY timelineContextChanged FINAL)
    Q_PROPERTY(ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY clipKeyChanged FINAL)
    Q_PROPERTY(ClipTime clipTime READ clipTime WRITE setClipTime NOTIFY clipTimeChanged FINAL)
    Q_PROPERTY(QColor noteColor READ noteColor WRITE setNoteColor NOTIFY noteColorChanged FINAL)
    Q_PROPERTY(bool isMidi READ isMidi NOTIFY isMidiChanged FINAL)

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };

public:
    MidiNotesView(QQuickItem* parent = nullptr);
    ~MidiNotesView() override = default;

    TimelineContext* timelineContext() const;
    void setTimelineContext(TimelineContext* newContext);
    ClipKey clipKey() const;
    void setClipKey(const ClipKey& newClipKey);
    ClipTime clipTime() const;
    void setClipTime(const ClipTime& newClipTime);
    QColor noteColor() const;
    void setNoteColor(const QColor& newColor);
    bool isMidi() const;

    void paint(QPainter* painter) override;

signals:
    void timelineContextChanged();
    void clipKeyChanged();
    void clipTimeChanged();
    void noteColorChanged();
    void isMidiChanged();

private:
    void updateIsMidi();

    TimelineContext* m_context = nullptr;
    ClipKey m_clipKey;
    ClipTime m_clipTime;
    QColor m_noteColor = QColor(255, 255, 255);
    bool m_isMidi = false;
};
}
