#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ingame
{
    // 16-bit PCM, channels interleaved
    struct Pcm
    {
        unsigned channels = 0, rate = 0;
        std::vector<std::int16_t> samples;
    };
    // decodes one FMOD sound-bank (FSB5/FSB4) sample: PCM8/PCM16, IMA ADPCM (FMOD's Xbox layout), FADPCM, MPEG
    // (minimp3) or Vorbis
    // (with the FMOD setup header named by `vorbis_setup`, the CRC32 from the sample's header). `frames` (when known)
    // trims the result. False with a reason when the codec isn't one of these or the data doesn't decode.
    bool decode_fmod_sample(const std::string &codec, const unsigned char *data, std::size_t bytes, unsigned channels, unsigned rate,
                            unsigned frames, std::uint32_t vorbis_setup, bool signed8, Pcm &out, std::string &why);
    // a RIFF WAVE file in memory
    std::vector<unsigned char> wav_file(const Pcm &pcm);
}
