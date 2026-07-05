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

//! Takes (and clears) the pending notes; empty if nothing was queued
EFFECTS_API std::vector<Note> Take();
}

#endif // __AUDACITY_MIDI_RENDER_QUEUE__
