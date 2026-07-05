/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiSequence.cpp

  (AU4 DAW fork) Note data attached to MIDI-flagged wave tracks.

**********************************************************************/
#include "MidiSequence.h"

#include <algorithm>

#include "WaveTrack.h"

static const ChannelGroup::Attachments::RegisteredFactory midiSequenceFactory {
    [](auto&) { return std::make_unique<MidiSequence>(); }
};

MidiSequence::MidiSequence() = default;

MidiSequence::MidiSequence(const MidiSequence& other)
    : mNotes{other.mNotes}
{
}

MidiSequence::~MidiSequence() = default;

std::unique_ptr<ClientData::Cloneable<> > MidiSequence::Clone() const
{
    return std::make_unique<MidiSequence>(*this);
}

MidiSequence& MidiSequence::Get(WaveTrack& track)
{
    return track.Attachments::Get<MidiSequence>(midiSequenceFactory);
}

const MidiSequence& MidiSequence::Get(const WaveTrack& track)
{
    return Get(const_cast<WaveTrack&>(track));
}

void MidiSequence::SetNotes(std::vector<MidiNote> notes)
{
    mNotes = std::move(notes);
}

void MidiSequence::AddNote(const MidiNote& note)
{
    mNotes.push_back(note);
}

void MidiSequence::Clear()
{
    mNotes.clear();
}

double MidiSequence::EndBeats() const
{
    double end = 0.0;
    for (const auto& note : mNotes) {
        end = std::max(end, note.startBeats + note.lengthBeats);
    }
    return end;
}

static constexpr auto Note_tag = "note";
static constexpr auto Start_attr = "start";
static constexpr auto Length_attr = "length";
static constexpr auto Pitch_attr = "pitch";
static constexpr auto Velocity_attr = "vel";

const std::string& MidiSequence::XMLTag()
{
    static const std::string result{ "midisequence" };
    return result;
}

bool MidiSequence::HandleXMLTag(
    const std::string_view& tag, const AttributesList& attrs)
{
    if (tag == XMLTag()) {
        // Deserialization replaces whatever the attachment held before
        mNotes.clear();
        return true;
    }

    if (tag == Note_tag) {
        MidiNote note;
        double dblValue;
        long nValue;
        for (const auto& pair : attrs) {
            const auto& attr = pair.first;
            const auto& value = pair.second;

            if (attr == Start_attr && value.TryGet(dblValue)) {
                note.startBeats = std::max(0.0, dblValue);
            } else if (attr == Length_attr && value.TryGet(dblValue)) {
                note.lengthBeats = std::max(0.0, dblValue);
            } else if (attr == Pitch_attr && value.TryGet(nValue)) {
                note.pitch = static_cast<int>(std::clamp<long>(nValue, 0, 127));
            } else if (attr == Velocity_attr && value.TryGet(dblValue)) {
                note.velocity = static_cast<float>(std::clamp(dblValue, 0.0, 1.0));
            }
        }
        mNotes.push_back(note);
        return true;
    }

    return false;
}

XMLTagHandler* MidiSequence::HandleXMLChild(const std::string_view& tag)
{
    if (tag == Note_tag) {
        // Notes are leaf elements handled by this same handler
        return this;
    }
    return nullptr;
}

void MidiSequence::WriteXML(XMLWriter& xmlFile) const
{
    // Ordinary audio tracks have no notes; don't pollute their XML
    if (mNotes.empty()) {
        return;
    }

    xmlFile.StartTag(XMLTag());
    for (const auto& note : mNotes) {
        xmlFile.StartTag(Note_tag);
        xmlFile.WriteAttr(Start_attr, note.startBeats);
        xmlFile.WriteAttr(Length_attr, note.lengthBeats);
        xmlFile.WriteAttr(Pitch_attr, note.pitch);
        xmlFile.WriteAttr(Velocity_attr, static_cast<double>(note.velocity));
        xmlFile.EndTag(Note_tag);
    }
    xmlFile.EndTag(XMLTag());
}

// Hook the attachment into WaveTrack's XML serialization
static WaveTrackIORegistry::ObjectReaderEntry midiSequenceReader {
    MidiSequence::XMLTag(),
    [](WaveTrack& track) { return &MidiSequence::Get(track); }
};

static WaveTrackIORegistry::ObjectWriterEntry midiSequenceWriter {
    [](const WaveTrack& track, auto& xmlFile) {
        MidiSequence::Get(track).WriteXML(xmlFile);
    }
};
