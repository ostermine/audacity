/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiRenderQueue.h

  (AU4 DAW fork) Hand-off of MIDI-track notes to the next
  generator-instrument render.

**********************************************************************/
#ifndef __AUDACITY_MIDI_RENDER_QUEUE__
#define __AUDACITY_MIDI_RENDER_QUEUE__

#include <vector>

//! The AU4 MIDI render service stores the track's notes here right before
//! applying the instrument as a generator effect; the instrument's instance
//! (e.g. VST3Instance) takes them in ProcessInitialize instead of its
//! placeholder pattern. Main-thread only, one render at a time.
namespace MidiRenderQueue {
struct Note {
    double timeSec{ 0.0 };
    double durationSec{ 0.0 };
    int pitch{ 60 };      //!< MIDI note number 0..127
    float velocity{ 0.8f }; //!< normalized 0..1
};

EFFECTS_API void Set(std::vector<Note> notes);

//! Pending notes; empty if nothing was queued. NOT cleared by reading:
//! one render flow may initialize processing several times (preview, apply),
//! so the render service clears the queue explicitly when done.
EFFECTS_API const std::vector<Note>& Get();

//! Implemented by instrument effect instances (e.g. VST3Instance) that can
//! play notes live during realtime processing. Lets the AU4 live-MIDI
//! scheduler feed notes without depending on plugin-format headers.
class EFFECTS_API LiveMidiReceiver
{
public:
    virtual ~LiveMidiReceiver() = default;

    //! Positions in samples from the realtime stream start
    virtual void QueueLiveNote(long long sampleTime, long long sampleDuration, int pitch, float velocity) = 0;
    //! Plays in the next processed block (piano roll audition)
    virtual void QueueLiveNoteNow(long long sampleDuration, int pitch, float velocity) = 0;
    //! Drops queued notes and restarts the sample clock from 0
    virtual void ResetLiveNotes() = 0;
};
}

#endif // __AUDACITY_MIDI_RENDER_QUEUE__
