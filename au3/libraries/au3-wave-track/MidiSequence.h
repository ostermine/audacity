/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiSequence.h

  (AU4 DAW fork) Note data attached to MIDI-flagged wave tracks.

**********************************************************************/
#ifndef __AUDACITY_MIDI_SEQUENCE__
#define __AUDACITY_MIDI_SEQUENCE__

#include <string>
#include <vector>

#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

class WaveTrack;

//! One note of a MIDI-flagged wave track.
//! Times are in quarter notes from time zero of the track, so changing the
//! project tempo keeps notes on their musical positions.
struct MidiNote {
    double startBeats{ 0.0 };  //!< start position in quarter notes
    double lengthBeats{ 1.0 }; //!< duration in quarter notes
    int pitch{ 60 };           //!< MIDI note number 0..127
    float velocity{ 0.8f };    //!< normalized 0..1
};

//! Attachment carrying the note list of a MIDI-flagged wave track.
//! Cloned together with the track, so undo/redo and track duplication work
//! without extra code; serialized as a <midisequence> child of <wavetrack>.
class WAVE_TRACK_API MidiSequence final : public ClientData::Cloneable<>, public XMLTagHandler
{
public:
    MidiSequence();
    MidiSequence(const MidiSequence&);
    MidiSequence& operator=(const MidiSequence&) = delete;
    ~MidiSequence() override;
    std::unique_ptr<ClientData::Cloneable<> > Clone() const override;

    static MidiSequence& Get(WaveTrack& track);
    static const MidiSequence& Get(const WaveTrack& track);

    const std::vector<MidiNote>& Notes() const { return mNotes; }
    void SetNotes(std::vector<MidiNote> notes);
    void AddNote(const MidiNote& note);
    void Clear();

    //! End of the last note in quarter notes (0 if there are no notes)
    double EndBeats() const;

    static const std::string& XMLTag();
    bool HandleXMLTag(
        const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view& tag) override;
    void WriteXML(XMLWriter& xmlFile) const;

private:
    std::vector<MidiNote> mNotes;
};

#endif // __AUDACITY_MIDI_SEQUENCE__
