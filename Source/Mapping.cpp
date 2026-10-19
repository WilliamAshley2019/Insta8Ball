#include "Mapping.h"

#include <algorithm>
#include <cstdio>

namespace icc
{

const char* gmDrumName (int note) noexcept
{
    static const char* names[] = {
        "High Q", "Slap", "Scratch Push", "Scratch Pull", "Sticks", "Square Click", "Metronome Click", "Metronome Bell",
        "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap", "Electric Snare",
        "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat", "Low-Mid Tom",
        "Hi-Mid Tom", "Crash Cymbal 1", "High Tom", "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell", "Tambourine",
        "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap", "Ride Cymbal 2", "Hi Bongo", "Low Bongo",
        "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale", "High Agogo", "Low Agogo",
        "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "Hi Wood Block",
        "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle", "Shaker", "Jingle Bell",
        "Belltree", "Castanets", "Mute Surdo", "Open Surdo" };
    if (note < kFirstDrumKey || note > kLastDrumKey) return "";
    return names[note - kFirstDrumKey];
}

const char* gmProgramName (int program) noexcept
{
    static const char* names[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1",
        "Electric Piano 2", "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
        "Xylophone", "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
        "Reed Organ", "Accordion", "Harmonica", "Tango Accordion", "Nylon Guitar", "Steel Guitar", "Jazz Guitar",
        "Clean Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics", "Acoustic Bass",
        "Fingered Bass", "Picked Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
        "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs",
        "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section",
        "Synth Brass 1", "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn",
        "Bassoon", "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle",
        "Ocarina", "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)",
        "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)",
        "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
        "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)",
        "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag Pipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot" };
    return names[std::clamp (program, 0, 127)];
}

const char* gmFamilyName (int program) noexcept
{
    static const char* fam[16] = { "Piano", "Chromatic Percussion", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass",
                                   "Reed", "Pipe", "Synth Lead", "Synth Pad", "Synth Effects", "Ethnic", "Percussive", "Sound Effects" };
    return fam[std::clamp (program, 0, 127) / 8];
}

std::string gmDrumKitName (int program)
{
    switch (std::clamp (program, 0, 127))
    {
        case 0:  return "Standard Kit";
        case 8:  return "Room Kit";
        case 16: return "Power Kit";
        case 24: return "Electronic Kit";
        case 25: return "TR-808 Kit";
        case 32: return "Jazz Kit";
        case 40: return "Brush Kit";
        case 48: return "Orchestra Kit";
        case 56: return "SFX Kit";
        default: return "Kit " + std::to_string (std::clamp (program, 0, 127)) + " (device specific)";
    }
}

namespace
{
    struct Hint { const char* role; const char* fl; };
    const Hint kHints[kNumTracks] = {
        { "lead line",   "any lead: FLEX/3x Osc/Sytrus, or a GM lead via the Soundfont Player" },
        { "chords",      "keys/stab patch; a GM piano or e-piano from a SoundFont works as is" },
        { "bass",        "mono bass patch; for 808-style styles use a long sub bass" },
        { "drums",       "FPC (GM layout) or any GM drum kit - channel 10 is the GM drum channel" },
        { "pad",         "slow-attack pad; keep it quiet, it plays long held chords" },
        { "arpeggio",    "plucky/bright synth; 16th-note stream, so short envelopes help" },
        { "fills",       "bell/pluck/harp; sparse decoration between melody notes" },
    };
}

std::string describeMapping (const std::array<int, kNumTracks>& channels, const std::array<int, kNumTracks>& programs,
                             bool programChangesSent)
{
    std::string s = "TRACK ROUTING (MIDI channel / General MIDI program)\n\n";
    char line[256];
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int ch = std::clamp (channels[(size_t) t], 1, 16), pr = std::clamp (programs[(size_t) t], 0, 127);
        if (t == Rhythm)
            std::snprintf (line, sizeof line, "%-8s ch %2d  GM drum keys, program %d = %s%s\n         %s\n", trackName (t), ch, pr + 1,
                           gmDrumKitName (pr).c_str(), ch == 10 ? " (standard drum channel)" : " (note: GM drums are normally ch 10)", kHints[t].fl);
        else
            std::snprintf (line, sizeof line, "%-8s ch %2d  program %3d  %s (%s)\n         %s: %s\n", trackName (t), ch, pr + 1,
                           gmProgramName (pr), gmFamilyName (pr), kHints[t].role, kHints[t].fl);
        s += line;
    }
    s += programChangesSent ? "\nProgram changes are ON: bank/program messages go out on each track's channel when playback starts.\n"
                            : "\nProgram changes are OFF (default): the program numbers above are suggestions only.\n"
                              "Turn on \"Send PC\" to emit them (useful for GM synths / Soundfont Player / MIDI Out).\n";

    s += "\nFL STUDIO NOTES\n"
         "- Route each track by MIDI channel: give every ICC track a different channel and point one\n"
         "  instrument per channel (MIDI Out plugin, or FL's per-channel output of this plugin).\n"
         "- FPC: its default layout follows the Akai MPC and its 'Empty' preset has pads on the General MIDI keys,\n"
         "  so the Rhythm track's keys (36 kick, 38 snare, 42 hat ...) hit the matching pads. Enable\n"
         "  'Show GM note names' in FPC to see the names below.\n"
         "- FL's MIDI Out plugin shows GM names for the Patch menu and, on channel 10, GM drum names in the\n"
         "  piano roll (editable in its 'GM Drums.txt').\n"
         "- FLEX is a preset synth and its manual does not describe General MIDI or multi-channel use, so treat\n"
         "  it as one preset per instance. For true GM playback use the Soundfont Player with a GM .sf2.\n";
    return s;
}

std::string describeDrumKeys()
{
    static const int keys[] = { gm::kick, gm::rim, gm::snare, gm::clap, gm::tomFloorLo, gm::hatClosed, gm::tomFloorHi, gm::hatPedal,
                                gm::tomLo, gm::hatOpen, 47, 48, gm::crash, gm::tomHi, gm::ride, gm::rideBell, gm::tambourine,
                                gm::splash, gm::cowbell, gm::congaHi, gm::congaLo, gm::maracas, gm::claves, gm::shaker };
    std::string s = "RHYTHM TRACK KEYS (General MIDI percussion)\n\nkey  note  FL name  drum\n";
    char line[96];
    for (int k : keys)
    {
        std::snprintf (line, sizeof line, "%3d  %-2s%-2d  (FL %s%d)  %s\n", k, noteName (k), k / 12 - 1, noteName (k), k / 12, gmDrumName (k));
        s += line;
    }
    s += "\nFL Studio's piano roll labels MIDI note 60 as C5 (one octave above the C4 convention), so key 36 shows as C3 there.\n";
    return s;
}

} // namespace icc
